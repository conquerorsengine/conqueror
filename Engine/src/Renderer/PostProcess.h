#pragma once

#include "Core/Base/Base.h"
#include "RHI/Framebuffer.h"
#include "RHI/Shader.h"
#include "RHI/VertexArray.h"
#include "Renderer/RenderSettings.h"
#include "Scene/Scene.h"

#include <memory>
#include <vector>

namespace Conqueror { class FSR2Upscaler; }

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
        PostProcessStack();   // tanımı PostProcess.cpp'de (FSR2Upscaler eksik tür)
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
        // Bu karede uygulanan kenar yumuşatma yöntemi ve yükseltici (seçilen yöntem bu sistemde yoksa en yakın karşılığı) ve iç
        // çözünürlük çarpanı (SSAA > 1, yükseltici < 1)
        AntiAliasingMode GetAppliedAntiAliasing() const { return m_AAMode; }
        UpscalerMode GetAppliedUpscaler() const { return m_Upscaler; }
        float GetRenderScale() const { return m_RenderScale; }
        std::shared_ptr<Framebuffer> GetSceneFramebuffer() const { return m_SceneFramebuffer; }

        // Eski API uyumluluğu (yalnızca AA; ton eşleme yok)
        void ApplyFXAA();
        void ApplySMAA();
        void ResetHistory() { m_HistoryValid = false; m_HasPrevViewProj = false; }
        // Bütün zamansal geçmişler (TAA, TAAU, FSR 2, SMAA T2x, kararlılık, GI, bulutlar) yeniden başlar: görüntünün bir kısmı birden
        // değişti (editörde ızgara / gizmo kapatıldı) ya da görünüm bir süre çizilmedi
        void ResetTemporalHistory();

        // Renderer3D: şu an bu görünümün sahne hedefine çiziliyorsa Temporal AA alt piksel titreşimi (NDC)
        static bool GetCurrentJitter(glm::vec2& outNDC);
        // Bu görünüm bu karede hareket verisi istiyor mu (nesne hareket vektörleri, hareket bayrağı, önceki konum): zamansal
        // yöntemler ve tek kare yöntemlerindeki zamansal kararlılık katmanı. Titreşim yalnız GetCurrentJitter ile uygulanır
        static bool WantsMotionData();
        // Şu an bu görünümün sahne hedefine çiziliyorsa ve tek kare yöntemlerinin zamansal kararlılık katmanı açıksa true
        // (tepki maskesi yazılamadığında, MSAA'da, parçacıklar ekran dikdörtgenlerini bildirir)
        static bool IsStabilityActive();
        // Şu an bu görünümün sahne hedefine çiziliyorsa ve yükseltici iç çözünürlükte çiziyorsa malzeme dokularının mip sapması
        // (log2(iç ölçek), eksi); yerel çözünürlükte / süper örneklemede 0
        static float GetCurrentTextureLodBias();
        // Şu an sahne hedefine çizilen görünüm (Renderer3D hareket maskesi anahtarı)
        static const void* GetCurrentStack();
        // Şu an bu görünümün (çok örneklemesiz) sahne hedefine çiziliyorsa sahne derinliği, değilse 0. Derinlik yazmayan
        // saydam çizimler (parçacıklar) örnekleyebilir: yüzeye yaklaşınca yumuşakça solarlar (soft particles)
        static uint32_t GetCurrentSceneDepth();
        // TAA tepki maskesi (reactive mask): derinlik / hareket bayrağı yazmayan saydam çizimler (parçacıklar) kapladıkları
        // pikselleri buraya yazar; TAA o piksellerde geçmişe güvenmez. Begin maskenin çerçeve tamponunu bağlar (bu görünümün
        // TAA'lı sahne hedefine çizilmiyorsa 0), End sahne hedefini geri bağlar
        static uint32_t BeginReactiveDraw();
        // Bu görünüm tepki maskesi kullanabiliyor mu (titreşimli tek örnekli sahne hedefi; çerçeve tamponu bağı sorulmaz):
        // hareket eden saydam mesh ekran dikdörtgeni yerine maskeye piksel piksel yazılır
        static bool CanDrawReactive();
        // GBAA / SRAA: bu görünümün opak geometrisi bu karede toplanıp özel gölgelendiriciyle yeniden çizilecek mi (sahne hedefi
        // bağlıyken Renderer3D sorar)
        static bool WantsGeometry();
        static void EndReactiveDraw();
        // SSR normal/pürüzlülük tamponu (yoksa 0)
        uint32_t GetSceneNormalTexture() const;
        // Güneş gölgesi (0 gölge .. 1 aydınlık), TAA açıkken: gölgesi değişen yüzey geçmişe güvenmez
        uint32_t GetSceneShadowMaskTexture() const;
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
        // Kenar yumuşatma ve yükseltme: seçilen yöntemlerin bu karedeki gerçek uygulaması (ortografik kamera, nokta büyütme,
        // sistemde olmayan yöntem, eski sahnelerin AA listesindeki yükselticiler)
        void ResolveAntiAliasing(const PostProcessSettings& settings, const RenderCamera& camera);
        // EQAA (AMD) / CSAA (NVIDIA): renk örneğinden fazla kapsam örnekli renderbuffer çerçeve tamponu
        bool CreateCoverageFramebuffer(uint32_t width, uint32_t height);
        void DestroyCoverageFramebuffer();
        // SMAA alan / arama dokuları (Jimenez, External/smaa/Textures)
        void EnsureSMAATextures();
        // NIS katsayı dokuları (2x64 RGBA32F) ve sabit tamponu
        void EnsureNISResources();
        // Nesne hız geçişi (TAA, SMAA T2x): hareket eden opak nesneler m_Velocity'ye; çizildiyse true
        bool RenderObjectVelocity(const RenderCamera& camera);
        // GBAA kenar uzaklığı (RG16F) ya da SRAA çok örnekli normal + derinlik hedefini çizer (son geçişten önce)
        void RenderAAGeometry(const RenderCamera& camera);
        void DestroySRAATargets();
        void EnsureCMAA2Resources(uint32_t width, uint32_t height);
        void DestroyCMAA2Resources();
        // CMAA2'yi texture üzerinde yerinde uygular (RGBA8, iç çözünürlük)
        void RunCMAA2(uint32_t texture);
        // MSAA renk çözümü (ton eşleme ağırlıklı; TXAA'da geniş çadır süzgeci)
        void ResolveMultisampleColor();
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
        // Full Screen Pass (Image Effect Shader): kaynak doku -> hedef
        bool RunFullScreenPass(const struct FullScreenPassEntry& pass, uint32_t source, RenderTarget& target, float time);
        RenderTarget m_AfterPass[2];
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
        RenderTarget m_SMAAWeights;    // SMAA / MLAA karışım ağırlıkları
        RenderTarget m_Temp;
        RenderTarget m_Temp2;          // iç çözünürlükte ikinci LDR ara hedef (zincirli son geçişler)
        RenderTarget m_Upscale;        // çıktı çözünürlüğünde ara hedef (FSR: EASU -> RCAS)
        uint32_t m_SMAAAreaTex = 0, m_SMAASearchTex = 0;
        uint32_t m_NISCoefScaler = 0, m_NISCoefUSM = 0, m_NISConstants = 0;   // NVIDIA Image Scaling katsayı dokuları + sabitler
        // Intel CMAA2 çalışma kaynakları (iç çözünürlükte): kenar ve liste başı dokuları (R32UI), tamponlar (şekil adayları,
        // ertelenmiş karışım konumları, karışım öğeleri, sayaçlar, dolaylı gönderim argümanları)
        uint32_t m_CMAA2Edges = 0, m_CMAA2Heads = 0;
        uint32_t m_CMAA2Buffers[5] = {};
        uint32_t m_CMAA2Width = 0, m_CMAA2Height = 0;
        // SMAA T2x: iki karenin SMAA çıktısı (LDR, alfa: hız büyüklüğü); T2x ve TAAU çözümü için bu karenin matrisleri / hız durumu
        RenderTarget m_T2xHistory[2];
        int m_T2xIndex = 0;
        bool m_T2xValid = false;
        bool m_FinalHasVelocity = false;
        glm::mat4 m_FinalInvViewProj{ 1.0f };
        // AMD FSR 2 (Upscaling > FSR 2, Anti-Aliasing > FSR AA): bağlam, tam ekran hareket vektörleri, çıktı ve kamera bilgisi
        std::unique_ptr<FSR2Upscaler> m_FSR2;
        RenderTarget m_MotionVectors;
        RenderTarget m_FSR2Reactive;    // FSR 2 tepki maskesi: m_Reactive + hareket eden nesnelerin gölge hacimleri (R8)
        RenderTarget m_FSR2PrevShadow;  // önceki karenin güneş gölgesi (FSR 2 tepki maskesi: gölgesi değişen pikseller)
        RenderTarget m_FSR2Output;
        bool m_UseFSR2 = false;
        // GBAA / SRAA geometri geçişi
        bool m_WantGeometry = false;      // sonraki kare için istek
        bool m_GeometryReady = false;     // bu karenin hedefi çizildi
        RenderTarget m_GBAAEdges;
        uint32_t m_GeometryFramebuffer = 0;
        uint32_t m_SRAAFramebuffer = 0, m_SRAANormalMS = 0, m_SRAADepthMS = 0;
        uint32_t m_SRAAWidth = 0, m_SRAAHeight = 0;
        bool m_FSR2Valid = false;
        float m_FinalNear = 0.1f, m_FinalFar = 1000.0f, m_FinalFovY = 1.0f;
        // TAAU: çıktı çözünürlüğünde geçmiş (RGBA16F, LDR)
        RenderTarget m_TAAUHistory[2];
        int m_TAAUIndex = 0;
        bool m_TAAUValid = false;
        glm::mat4 m_FinalViewProj{ 1.0f };   // bu karenin titreşimsiz görüş-izdüşümü (TAAU açılma testi)
        // Önceki karenin sahne derinliği (kare sonunda kopyalanır): TAA / TAAU açılma testi. Pikselin kendi noktası önceki karede
        // daha yakın bir yüzeyin arkasındaysa ve o yüzey uzaklaştıysa geçmiş o örtücünündür (arka kenarda iz / çentik)
        uint32_t m_PrevDepth = 0;
        uint32_t m_PrevDepthWidth = 0, m_PrevDepthHeight = 0, m_PrevDepthFormat = 0;
        bool m_PrevDepthValid = false;
        void SavePrevDepth(bool wanted);
        bool PrevDepthUsable() const;
        // Kenar yumuşatma planı (Renderer/AntiAliasing.h): bu karede uygulanan ve sonraki kare için istenen
        AntiAliasingMode m_AAMode = AntiAliasingMode::None;
        UpscalerMode m_Upscaler = UpscalerMode::None;
        bool m_CASSharpen = false;                                 // keskinleştirme AMD CAS ile
        float m_CASStrength = 0.0f;
        float m_RenderScale = 1.0f, m_WantRenderScale = 1.0f;     // iç çözünürlük / çıktı çözünürlüğü
        uint32_t m_OutputWidth = 0, m_OutputHeight = 0;           // panelin gerçek boyutu
        bool m_SampleShading = false, m_WantSampleShading = false; // SGSSAA / RGSS: her MSAA örneğinde gölgelendirme
        uint32_t m_CoverageSamples = 0, m_WantCoverageSamples = 0; // EQAA / CSAA kapsam örneği (0 = kapalı)
        uint32_t m_CoverageColorSamples = 0;
        bool m_CoverageNV = false;                                 // CSAA (NV) mı EQAA (AMD) mı
        uint32_t m_CoverageFramebuffer = 0;
        std::vector<uint32_t> m_CoverageRenderbuffers;
        bool m_TXAA = false, m_WantTXAA = false;                   // MSAA + TAA (titreşim MSAA ile birlikte)
        bool m_MultisampleTAA = false, m_WantMultisampleTAA = false;   // Temporal Stability: MSAA / CSAA / EQAA / RGSS / SGSSAA + TAA çözümü
        bool IsSceneDrawTarget(uint32_t framebuffer) const;        // bu kare sahnenin çizildiği çerçeve tamponu (EQAA / CSAA: kapsam örnekli olan)
        int m_TAAVariant = 0, m_WantTAAVariant = 0;                // 0 TAA, 1 TSAA, 2 TSSAA
        uint32_t m_SamplePositionsFor = 0;                         // u_SamplePos önbelleğinin örnek sayısı
        float m_SamplePositions[32] = {};
        uint32_t m_BloomResult = 0;
        bool m_AOValid = false;
        bool m_LitScene = true;
        bool m_PointUpscale = false;   // Pixel Perfect Camera: düşük çözünürlüklü sahne en yakın komşu ile büyütülür
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_Samples = 1;
        uint32_t m_WantSamples = 1;

        // Efekt zinciri
        uint32_t m_Source = 0;          // zincirin şu anki HDR kaynağı (sahne ya da bir efekt çıktısı)
        bool m_AOApplied = false;       // AO sahne efektlerinde uygulandı (birleştirmede tekrar uygulanmaz)
        RenderTarget m_HDR[2];          // RGBA16F ara hedefler (sırayla)
        RenderTarget m_SSR, m_Fog, m_Clouds, m_DOFHalf, m_DOFBlur;
        RenderTarget m_History[2];      // TAA geçmişi
        int m_HistoryIndex = 0;
        // TAA tutarsızlık sayacı (R16F, geçmişle aynı sırada): geçmiş kaç karedir üst üste komşuluk kutusunun dışında.
        // Jitter titremesi bir kare tutar bir kare tutmaz (güven sürer); hareket eden gölge üst üste tutmaz (güven kalkar)
        RenderTarget m_TAAState[2];
        uint32_t m_TAAFramebuffer[2] = { 0, 0 };     // renk 0: geçmiş, renk 1: durum
        // Zamansal kararlılık (tek kare yöntemleri): önceki çıktı + durum (titreme puanı, en yakın uzaklık), sıra sıra
        RenderTarget m_StabHistory[2];
        RenderTarget m_StabState[2];
        uint32_t m_StabFramebuffer[2] = { 0, 0 };
        uint32_t m_StabIndex = 0;
        bool m_StabValid = false;
        bool m_WantStability = false;   // sonraki karede kararlılık katmanı (hareket verisi toplanır)
        bool m_StabilityActive = false; // bu kare
        glm::mat4 m_StabInvViewProj{ 1.0f };   // kararlılık katmanı: bu karenin titreşimsiz ters görüş-izdüşümü
        RenderTarget m_TAADisplay;                    // duman / kıvılcım / alev olan karede gösterilen görüntü (bunlar geçmişe yazılmaz)
        int m_LightChangeFrames = 0;                  // üst üste kaç karedir bir ışığın gücü değişiyor
        RenderTarget m_TAADebug;                      // r.TAADebug: renk 2, pikselin geçmiş yolu (yalnız gösterilir)
        RenderTarget m_Reactive;                      // TAA tepki maskesi (R8): parçacıkların kapladığı pikseller
        uint32_t m_ReactiveFramebuffer = 0;           // renk: m_Reactive, derinlik: sahne derinliği (salt okunur)
        bool m_ReactiveUsed = false;                  // bu karede maskeye çizildi (ilk çizimde temizlenir)
        RenderTarget m_Velocity;                      // nesne hareket vektörleri (RGBA16F: xy uv hızı, z 1 kesin / 0.5 yaklaşık)
        uint32_t m_VelocityFramebuffer = 0;           // renk: m_Velocity, derinlik: sahne derinliği (salt okunur)
        bool m_HistoryValid = false;
        uint32_t m_FrameIndex = 0;
        glm::vec2 m_JitterNDC{ 0.0f };
        bool m_JitterActive = false;    // bu karede sahne titreşimli çizildi
        bool m_CameraStill = false;     // bu karede kamera durağan (TAA + FXAA geçişi için)
        bool m_WantTAA = false;         // sonraki karede titreşim uygula
        bool m_WantNormals = false;     // sahne hedefinde SSR normal eki
        bool m_HasNormals = false;
        bool m_WantAlbedo = false;      // SSGI albedo eki (normal ekiyle birlikte)
        bool m_HasAlbedo = false;
        bool m_HasShadowMask = false;
        RenderTarget m_GI, m_GITemp, m_GIHistory[2];
        int m_GIHistoryIndex = 0;
        bool m_GIHistoryValid = false;
        glm::mat4 m_PrevViewProj{ 1.0f };
        bool m_HasPrevViewProj = false;
        float m_LastTime = -1.0f;
        glm::vec2 m_CloudWind{ 0.0f };
        RenderTarget m_CloudHistory[2];         // bulutların zamansal birikimi (çeyrek çözünürlük)
        int m_CloudHistoryIndex = 0;
        bool m_CloudHistoryValid = false;
        glm::mat4 m_PrevCloudViewProj{ 1.0f };  // önceki karede bulutların hesaplandığı matris
        float m_DeltaTime = 0.0f;
        double m_LastRunSeconds = -1.0;   // bu yığının en son çalıştığı an (s, tekdüze saat): bayat geçmiş testi
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
        // Full Screen Pass shader'ının son derleme hatası (Inspector gösterir)
        static std::string GetFullScreenPassError(const std::string& path);
    };
}
