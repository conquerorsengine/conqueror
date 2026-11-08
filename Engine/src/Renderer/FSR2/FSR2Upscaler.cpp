#include "FSR2Upscaler.h"

#include "FSR2GL.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Conqueror
{
    struct FSR2Upscaler::State
    {
        FfxFsr2Context Context{};
        std::vector<uint8_t> Scratch;
        bool Created = false;
        bool Failed = false;
        bool LoggedDispatchError = false;
        uint32_t RenderW = 0, RenderH = 0, DisplayW = 0, DisplayH = 0;
    };

    namespace
    {
        void OnMessage(FfxFsr2MsgType type, const wchar_t* message)
        {
            const std::wstring w(message ? message : L"");
            const std::string s(w.begin(), w.end());
            if (type == FFX_FSR2_MESSAGE_TYPE_ERROR)
                CQ_CORE_ERROR("FSR 2: {0}", s);
            else
                CQ_CORE_WARN("FSR 2: {0}", s);
        }
    }

    FSR2Upscaler::FSR2Upscaler() : m_State(std::make_unique<State>()) {}

    FSR2Upscaler::~FSR2Upscaler()
    {
        Destroy();
    }

    bool FSR2Upscaler::HasFailed() const
    {
        return m_State->Failed;
    }

    void FSR2Upscaler::Destroy()
    {
        if (m_State && m_State->Created)
        {
            ffxFsr2ContextDestroy(&m_State->Context);
            m_State->Created = false;
        }
    }

    glm::vec2 FSR2Upscaler::JitterOffset(uint32_t frameIndex, uint32_t renderWidth, uint32_t displayWidth)
    {
        const int32_t phases = std::max(ffxFsr2GetJitterPhaseCount((int32_t)renderWidth, (int32_t)std::max(displayWidth, renderWidth)), 1);
        float x = 0.0f, y = 0.0f;
        ffxFsr2GetJitterOffset(&x, &y, (int32_t)(frameIndex % (uint32_t)phases), phases);
        return { x, y };
    }

    bool FSR2Upscaler::Dispatch(const Params& p)
    {
        State& s = *m_State;
        if (s.Failed)
            return false;
        if (s.Created && (s.RenderW != p.RenderWidth || s.RenderH != p.RenderHeight || s.DisplayW != p.DisplayWidth || s.DisplayH != p.DisplayHeight))
            Destroy();
        bool reset = p.Reset;
        if (!s.Created)
        {
            s.Scratch.assign(FSR2GL::GetScratchMemorySize(), 0);
            FfxFsr2ContextDescription desc{};
            if (FSR2GL::GetInterface(&desc.callbacks, s.Scratch.data(), s.Scratch.size(), "Engine/src/Shaders/Include/ThirdParty/FSR2") != FFX_OK)
            {
                s.Failed = true;
                return false;
            }
            // LDR (ton eşlenmiş) giriş, iç çözünürlükte titreşimsiz hareket vektörleri, standart (ters olmayan) derinlik.
            // Otomatik pozlama kapalı (pozlama 1): giriş zaten ton eşlenmiş. Açıkken FSR 2 ince ayrıntı kilitlerini ve parlaklık
            // geçmişini kendi ölçtüğü pozlamayla hesaplıyor, dururken bile ince çizgiler / merdiven kenarları 8 karelik titreşim
            // döngüsüyle yanıp sönüyordu
            desc.flags = 0;
            desc.maxRenderSize = { p.RenderWidth, p.RenderHeight };
            desc.displaySize = { p.DisplayWidth, p.DisplayHeight };
            desc.device = (FfxDevice)(uintptr_t)1;
            desc.fpMessage = OnMessage;
            const FfxErrorCode e = ffxFsr2ContextCreate(&s.Context, &desc);
            if (e != FFX_OK)
            {
                CQ_CORE_ERROR("FSR 2: context creation failed (0x{0:x}); falling back to FSR 1", (uint32_t)e);
                s.Failed = true;
                return false;
            }
            s.Created = true;
            s.RenderW = p.RenderWidth;
            s.RenderH = p.RenderHeight;
            s.DisplayW = p.DisplayWidth;
            s.DisplayH = p.DisplayHeight;
            reset = true;
        }
        FfxFsr2DispatchDescription d{};
        d.commandList = (FfxCommandList)(uintptr_t)1;
        d.color = FSR2GL::GetTextureResource(p.Color, p.RenderWidth, p.RenderHeight, p.ColorFormat, L"FSR2_InputColor");
        d.depth = FSR2GL::GetTextureResource(p.Depth, p.RenderWidth, p.RenderHeight, p.DepthFormat, L"FSR2_InputDepth", true);
        d.motionVectors = FSR2GL::GetTextureResource(p.Motion, p.RenderWidth, p.RenderHeight, GL_RG16F, L"FSR2_InputMotionVectors");
        if (p.Reactive)
            d.reactive = FSR2GL::GetTextureResource(p.Reactive, p.RenderWidth, p.RenderHeight, GL_R8, L"FSR2_InputReactive");
        d.output = FSR2GL::GetTextureResource(p.Output, p.DisplayWidth, p.DisplayHeight, p.OutputFormat, L"FSR2_Output");
        d.output.state = FFX_RESOURCE_STATE_UNORDERED_ACCESS;
        d.jitterOffset = { p.JitterPixels.x, p.JitterPixels.y };
        d.motionVectorScale = { (float)p.RenderWidth, (float)p.RenderHeight };   // doku uv birimi -> FSR 2 sabiti 1
        d.renderSize = { p.RenderWidth, p.RenderHeight };
        d.enableSharpening = p.Sharpen;
        d.sharpness = std::clamp(p.Sharpness, 0.0f, 1.0f);
        d.frameTimeDelta = std::max(p.FrameTimeMs, 0.1f);
        d.preExposure = 1.0f;
        d.reset = reset;
        d.cameraNear = p.Near;
        d.cameraFar = p.Far;
        d.cameraFovAngleVertical = p.FovY;
        d.viewSpaceToMetersFactor = 1.0f;
        const FfxErrorCode e = ffxFsr2ContextDispatch(&s.Context, &d);
        if (e != FFX_OK)
        {
            if (!s.LoggedDispatchError)
                CQ_CORE_ERROR("FSR 2: dispatch failed (0x{0:x})", (uint32_t)e);
            s.LoggedDispatchError = true;
            return false;
        }
        return true;
    }
}
