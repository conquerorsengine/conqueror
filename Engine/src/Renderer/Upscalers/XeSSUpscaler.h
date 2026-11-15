#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

namespace Conqueror
{
    // Intel XeSS (Upscaling > XeSS, Anti-Aliasing > XeSS AA): Intel'in XeSS çalışma zamanı (libxess.dll, yalnız Windows) çalışma
    // anında açılır; başlıklar External/xess (MIT). Motor OpenGL ile çizer; XeSS'in Vulkan yolu OpenGL <-> Vulkan görüntü paylaşımıyla
    // (VkGLInterop) çalışır. Girdiler DLSS / FSR 2 ile aynı.
    class XeSSUpscaler
    {
    public:
        XeSSUpscaler();
        ~XeSSUpscaler();

        struct Params
        {
            uint32_t Color = 0, Depth = 0, Motion = 0, Output = 0;
            uint32_t RenderWidth = 0, RenderHeight = 0;
            uint32_t DisplayWidth = 0, DisplayHeight = 0;
            glm::vec2 JitterPixels{ 0.0f };
            int Quality = 1;          // UpscaleQualityMode
            bool Native = false;      // XeSS AA
            bool Reset = false;
        };

        bool Dispatch(const Params& params);
        void Destroy();
        bool HasFailed() const;

        // Bu sistem XeSS çalıştırabilir mi (Windows + libxess.dll + Vulkan aygıtı XeSS'in istediklerini destekliyor); sonuç saklanır
        static bool IsAvailable(std::string& reason);

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
