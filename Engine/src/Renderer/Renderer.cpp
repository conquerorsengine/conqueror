#include "Renderer.h"
#include "Particles/ParticleSystem.h"
#include "Grid/GridSystem.h"
#include "Renderer/RibbonRenderer.h"
#include "UI/UISystem.h"
#include "ShaderCompiler/ShaderCompiler.h"
#include "Renderer2D.h"
#include "TextRenderer.h"
#include "PostProcess.h"
#include "EditorGrid.h"
#include "Scene/Lighting2D.h"
#include "Scene/Billboards.h"
#include "AISystem/Navigation/NavAgents.h"
#include "Scene/VideoPlayer.h"
#include "Utilities/Renderer3D/Renderer3D.h"

namespace Conqueror
{
    RendererStats Renderer::s_Stats;

    void Renderer::Init()
    {
        ShaderCompiler::Init();
        RenderCommand::Init();
        Renderer2D::Init();
        Renderer3D::Init();
        TextRenderer::Init();
        PostProcess::Init();
        
        // Stats'ı sıfırla ama memory tracking'i koru
        s_Stats.DrawCalls = 0;
        s_Stats.Vertices = 0;
        s_Stats.Indices = 0;
        s_Stats.Triangles = 0;
        s_Stats.QuadCount = 0;
        s_Stats.SpriteCount = 0;
        s_Stats.TextCount = 0;
        s_Stats.MeshCount = 0;
        s_Stats.LightCount = 0;
    }

    void Renderer::Shutdown()
    {
        EditorGrid::Shutdown();
        PostProcess::Shutdown();
        TextRenderer::Shutdown();
        ParticleSystem::Shutdown();
        RibbonRenderer::Shutdown();
        GridSystem::Shutdown();
        UISystem::Shutdown();
        Lighting2D::Shutdown();
        Billboards::Shutdown();
        NavAgents::Shutdown();
        VideoPlayers::Shutdown();
        Renderer3D::Shutdown();
        Renderer2D::Shutdown();
        ShaderCompiler::Shutdown();
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height)
    {
        RenderCommand::SetViewport(0, 0, width, height);
    }
}
