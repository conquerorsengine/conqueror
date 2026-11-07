#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace Conqueror
{
    // AMD FidelityFX Super Resolution 2 bağlamı (OpenGL arka ucu: FSR2GL). İç / çıktı boyutu değişince yeniden kurulur.
    // FSR 2 hem yükseltici (Upscaling > FSR 2) hem yerel çözünürlükte kenar yumuşatma (Anti-Aliasing > FSR AA) olarak çalışır.
    class FSR2Upscaler
    {
    public:
        FSR2Upscaler();
        ~FSR2Upscaler();

        struct Params
        {
            uint32_t Color = 0, ColorFormat = 0;       // iç çözünürlükte renk (ton eşlenmiş LDR)
            uint32_t Depth = 0, DepthFormat = 0;       // iç çözünürlükte derinlik
            uint32_t Motion = 0;                       // RG16F hareket vektörleri (uv, önceki - şimdiki, titreşimsiz)
            uint32_t Reactive = 0;                     // R8 tepki maskesi (parçacıklar; 0 = yok)
            uint32_t Output = 0, OutputFormat = 0;     // çıktı çözünürlüğünde görüntü yazılabilir doku
            uint32_t RenderWidth = 0, RenderHeight = 0;
            uint32_t DisplayWidth = 0, DisplayHeight = 0;
            glm::vec2 JitterPixels{ 0.0f };            // bu karenin titreşimi (iç çözünürlük pikseli)
            float Near = 0.1f, Far = 1000.0f, FovY = 1.0f;
            float FrameTimeMs = 16.6f;
            bool Reset = false;
            bool Sharpen = false;
            float Sharpness = 0.5f;
        };

        bool Dispatch(const Params& params);
        void Destroy();
        bool HasFailed() const;

        // FSR 2'nin önerdiği titreşim dizisi (Halton 2,3; evre sayısı 8 x ölçek²), iç çözünürlük pikseli
        static glm::vec2 JitterOffset(uint32_t frameIndex, uint32_t renderWidth, uint32_t displayWidth);

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
