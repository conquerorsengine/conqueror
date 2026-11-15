#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

namespace Conqueror
{
    // NVIDIA DLSS (Upscaling > DLSS, Anti-Aliasing > DLAA): isteğe bağlı eklenti. NVIDIA'nın DLSS SDK'sı depoda değil (NVIDIA RTX SDKs
    // lisansı açık kaynak dağıtıma izin vermez): Tools/DLSS/fetch-dlss-sdk.sh ile indirilip -DCQ_DLSS_SDK_DIR=<klasör> verilince derlenir
    // (CQ_DLSS_ENABLED). Motor OpenGL ile çizer; DLSS NGX'in Vulkan yolundan, OpenGL <-> Vulkan görüntü paylaşımıyla çalışır
    // (VkGLInterop). Girdiler FSR 2 ile aynı: iç çözünürlükte renk (ton eşlenmiş), derinlik, hareket vektörleri; çıktı çözünürlüğünde görüntü.
    class DLSSUpscaler
    {
    public:
        DLSSUpscaler();
        ~DLSSUpscaler();

        struct Params
        {
            uint32_t Color = 0;                     // RGBA8 iç çözünürlükte renk
            uint32_t Depth = 0;                     // iç çözünürlükte derinlik (OpenGL, 0 yakın / 1 uzak)
            uint32_t Motion = 0;                    // RG16F hareket vektörleri (uv, önceki - şimdiki, titreşimsiz)
            uint32_t Output = 0;                    // RGBA8 çıktı çözünürlüğünde görüntü
            uint32_t RenderWidth = 0, RenderHeight = 0;
            uint32_t DisplayWidth = 0, DisplayHeight = 0;
            glm::vec2 JitterPixels{ 0.0f };         // bu karenin titreşimi (iç çözünürlük pikseli, GL: y yukarı)
            int Quality = 1;                        // UpscaleQualityMode (0 Ultra Quality .. 4 Ultra Performance, 5 Custom)
            bool Native = false;                    // DLAA (yerel çözünürlükte kenar yumuşatma)
            bool Reset = false;                     // geçmiş geçersiz (kamera kesmesi)
            float FrameTimeMs = 16.6f;
        };

        bool Dispatch(const Params& params);
        void Destroy();
        bool HasFailed() const;

        // Bu derlemede DLSS var mı (SDK ile derlendi mi)
        static bool IsCompiled();
        // Bu GPU + sürücü DLSS çalıştırabilir mi: NGX'e sorulur (bir kez, sonuç saklanır). false ise reason neden
        static bool IsAvailable(std::string& reason);

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
