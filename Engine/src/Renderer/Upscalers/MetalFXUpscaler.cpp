#include "MetalFXUpscaler.h"

// Apple dışı: MetalFX yok (Apple'da uygulama GraphicsAPIs/Metal/MetalFXUpscaler.mm)
#if !defined(__APPLE__)

namespace Conqueror
{
    struct MetalFXUpscaler::State
    {
    };

    MetalFXUpscaler::MetalFXUpscaler() : m_State(std::make_unique<State>()) {}
    MetalFXUpscaler::~MetalFXUpscaler() = default;
    bool MetalFXUpscaler::Dispatch(const Params&) { return false; }
    void MetalFXUpscaler::Destroy() {}
    bool MetalFXUpscaler::HasFailed() const { return true; }

    bool MetalFXUpscaler::IsAvailable(std::string& reason)
    {
        reason = "Runs only on Apple GPUs with the Metal graphics API (macOS 13 / iOS 16 or newer).";
        return false;
    }
}

#endif
