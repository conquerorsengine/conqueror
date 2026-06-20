#pragma once

#include "Core/Base/Base.h"
#include "RHI/Framebuffer.h"
#include "RHI/Shader.h"
#include "RHI/VertexArray.h"
#include "Scene/Scene.h"

#include <memory>
#include <vector>

namespace Conqueror
{
    class CQ_API PostProcess
    {
    public:
        static void Init();
        static void Shutdown();

        static void BeginScene(uint32_t width, uint32_t height);
        static void EndScene();

        static void ApplyFXAA();
        static void ApplySMAA();
        static void ApplyHaloFlare(const glm::vec2& lightScreenPos, const glm::vec3& lightColor, 
                                   bool lightVisible, bool haloEnabled, std::shared_ptr<class Texture2D> haloTexture, 
                                   float haloStrength, bool flareEnabled, float flareStrength,
                                   const std::vector<struct Scene::FlareElement>& flareElements);

        static std::shared_ptr<Framebuffer> GetSceneFramebuffer() { return s_SceneFramebuffer; }
        static uint32_t GetFinalTextureID();

    private:
        static void CreateQuad();

    private:
        static std::shared_ptr<Framebuffer> s_SceneFramebuffer;
        static std::shared_ptr<Framebuffer> s_ResolvedFramebuffer;
        static std::shared_ptr<Framebuffer> s_IntermediateFramebuffer; // Halo/Flare için
        static std::shared_ptr<Framebuffer> s_SMAAEdgeFramebuffer;
        static std::shared_ptr<Shader> s_FXAAShader;
        static std::shared_ptr<Shader> s_SMAAEdgeShader;
        static std::shared_ptr<Shader> s_SMAABlendShader;
        static std::shared_ptr<Shader> s_HaloFlareShader;
        static std::shared_ptr<VertexArray> s_QuadVA;
        static std::shared_ptr<VertexBuffer> s_QuadVB;
        static std::shared_ptr<IndexBuffer> s_QuadIB;

        static uint32_t s_Width, s_Height;
    };
}
