#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

namespace Conqueror
{
    // Apple MetalFX zamansal yükseltici (Upscaling > MetalFX): yalnız Metal arka ucunda (macOS 13 / iOS 16 ve sonrası, Apple GPU).
    // GL dokularının Metal karşılıkları MetalFX'e verilir (MtlGL); girdiler DLSS / FSR 2 ile aynı.
    class MetalFXUpscaler
    {
    public:
        MetalFXUpscaler();
        ~MetalFXUpscaler();

        struct Params
        {
            uint32_t Color = 0, Depth = 0, Motion = 0, Output = 0;
            uint32_t RenderWidth = 0, RenderHeight = 0;
            uint32_t DisplayWidth = 0, DisplayHeight = 0;
            glm::vec2 JitterPixels{ 0.0f };
            bool Reset = false;
        };

        bool Dispatch(const Params& params);
        void Destroy();
        bool HasFailed() const;

        // Metal arka ucu + bu GPU MetalFX zamansal yükselticisini destekliyor mu
        static bool IsAvailable(std::string& reason);

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
