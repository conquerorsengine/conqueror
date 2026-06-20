#pragma once

#include "RHI/RenderCommand.h"
#include "RHI/RendererAPI.h"
#include "Core/Debug/RendererStats.h"

namespace Conqueror
{
    class Renderer
    {
    public:
        static void Init();
        static void Shutdown();

        static void OnWindowResize(uint32_t width, uint32_t height);

        static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
        
        static RendererStats& GetStats() { return s_Stats; }
        static void ResetStats() { s_Stats.Reset(); }

    private:
        static RendererStats s_Stats;
    };
}
