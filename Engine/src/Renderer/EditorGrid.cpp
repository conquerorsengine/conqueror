#include "EditorGrid.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Renderer.h"
#include "Renderer/PostProcess.h"

#include <glad/glad.h>

namespace Conqueror
{
    namespace
    {
        std::shared_ptr<Shader> s_GridShader;
        uint32_t s_EmptyVAO = 0;
        bool s_LoadFailed = false;
    }

    EditorGridSettings& EditorGrid::Settings()
    {
        static EditorGridSettings settings;
        return settings;
    }

    void EditorGrid::Render(const RenderCamera& camera, bool plane2D, bool linearOutput, bool background)
    {
        const auto& settings = Settings();
        if (!settings.Visible || s_LoadFailed)
            return;

        if (!s_GridShader)
        {
            s_GridShader = Shader::Create("Engine/src/Shaders/Editor/Grid.cqsh");
            if (!s_GridShader || !s_GridShader->IsValid())
            {
                s_LoadFailed = true;
                return;
            }
            glGenVertexArrays(1, &s_EmptyVAO);
        }

        // TAA: sahne bu karede alt piksel titretilmiş projeksiyonla çizildi; ızgara da aynısıyla çizilir. Aksi halde alçak
        // açılarda yarım piksellik fark zeminde santimlerce derinlik farkı olur, bazı karelerde ızgaranın tamamı zeminin
        // arkasında kalır (ızgara bir görünüp bir kaybolur)
        glm::mat4 projection = camera.Projection;
        glm::vec2 jitter;
        if (PostProcessStack::GetCurrentJitter(jitter))
            for (int c = 0; c < 4; ++c)
            {
                projection[c][0] += jitter.x * projection[c][3];
                projection[c][1] += jitter.y * projection[c][3];
            }
        const glm::mat4 viewProj = projection * camera.View;

        GLboolean blend = glIsEnabled(GL_BLEND);
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

        GLint depthFunc = GL_LESS;
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);

        // Derinlik testi örnek başına (MSAA) yapılır: nesne kenarları temiz kalır. Zeminle eşit derinlik yarışını
        // gölgelendirici çözer (ızgara kameraya doğru çok küçük bir pay kadar öne alınır)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        s_GridShader->Bind();
        s_GridShader->SetMat4("u_InvViewProjection", glm::inverse(viewProj));
        s_GridShader->SetMat4("u_ViewProjection", viewProj);
        s_GridShader->SetFloat3("u_CameraPos", camera.Position);
        s_GridShader->SetInt("u_Plane", plane2D ? 1 : 0);
        s_GridShader->SetFloat("u_BaseSize", glm::max(settings.BaseSize, 0.001f));
        s_GridShader->SetFloat("u_Opacity", settings.Opacity);
        s_GridShader->SetFloat("u_FadeDistance", settings.FadeDistance);
        s_GridShader->SetInt("u_LinearOutput", linearOutput ? 1 : 0);
        s_GridShader->SetInt("u_Orthographic", camera.Orthographic ? 1 : 0);
        s_GridShader->SetFloat3("u_MinorColor", settings.MinorColor);
        s_GridShader->SetFloat3("u_MajorColor", settings.MajorColor);
        s_GridShader->SetInt("u_Background", background ? 1 : 0);
        // Derinlik kuantalamasının mesafe hatası: perspektifte ~d² / (near · 2^22), ortografikte (far - near) / 2^22
        // (24 bit / float derinlik + enterpolasyon payı); öne alma payı bundan büyük tutulur
        const float steps = 4194304.0f;
        const float nearClip = glm::max(camera.NearClip, 1e-4f);
        s_GridShader->SetFloat("u_DepthErrorQuad", camera.Orthographic ? 0.0f : 1.0f / (nearClip * steps));
        s_GridShader->SetFloat("u_DepthErrorConst", camera.Orthographic ? glm::max(camera.FarClip - camera.NearClip, 0.0f) / steps : 0.0f);

        glBindVertexArray(s_EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        Renderer::GetStats().DrawCalls++;

        glDepthMask(depthMask);
        glDepthFunc(depthFunc);
        if (!blend) glDisable(GL_BLEND);
        if (!depthTest) glDisable(GL_DEPTH_TEST);
        if (cull) glEnable(GL_CULL_FACE);
    }

    void EditorGrid::Shutdown()
    {
        s_GridShader.reset();
        if (s_EmptyVAO)
        {
            glDeleteVertexArrays(1, &s_EmptyVAO);
            s_EmptyVAO = 0;
        }
    }
}
