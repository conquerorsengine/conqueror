#include "EnvironmentMap.h"
#include "Renderer/RHI/Shader.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Conqueror
{
    namespace
    {
        uint32_t s_CubeVAO = 0, s_CubeVBO = 0;
        uint32_t s_QuadVAO = 0, s_QuadVBO = 0;
        uint32_t s_BRDFLUT = 0;
        std::shared_ptr<Shader> s_PrefilterShader;
        std::shared_ptr<Shader> s_SkyShader;
        std::shared_ptr<Shader> s_BRDFShader;

        struct GLStateGuard
        {
            GLint Viewport[4]{};
            GLint Framebuffer{};
            GLboolean DepthTest;
            GLboolean Cull;
            GLboolean Blend;
            GLStateGuard()
            {
                glGetIntegerv(GL_VIEWPORT, Viewport);
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &Framebuffer);
                DepthTest = glIsEnabled(GL_DEPTH_TEST);
                Cull = glIsEnabled(GL_CULL_FACE);
                Blend = glIsEnabled(GL_BLEND);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                glDisable(GL_BLEND);
            }
            ~GLStateGuard()
            {
                glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
                glViewport(Viewport[0], Viewport[1], Viewport[2], Viewport[3]);
                if (DepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
                if (Cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
                if (Blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            }
        };

        void EnsureGeometry()
        {
            if (!s_CubeVAO)
            {
                const float v[] = {
                    -1, 1,-1, -1,-1,-1,  1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
                    -1,-1, 1, -1,-1,-1, -1, 1,-1, -1, 1,-1, -1, 1, 1, -1,-1, 1,
                     1,-1,-1,  1,-1, 1,  1, 1, 1,  1, 1, 1,  1, 1,-1,  1,-1,-1,
                    -1,-1, 1, -1, 1, 1,  1, 1, 1,  1, 1, 1,  1,-1, 1, -1,-1, 1,
                    -1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1,
                    -1,-1,-1, -1,-1, 1,  1,-1,-1,  1,-1,-1, -1,-1, 1,  1,-1, 1 };
                glGenVertexArrays(1, &s_CubeVAO);
                glGenBuffers(1, &s_CubeVBO);
                glBindVertexArray(s_CubeVAO);
                glBindBuffer(GL_ARRAY_BUFFER, s_CubeVBO);
                glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
                glBindVertexArray(0);
            }
            if (!s_QuadVAO)
            {
                const float q[] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
                glGenVertexArrays(1, &s_QuadVAO);
                glGenBuffers(1, &s_QuadVBO);
                glBindVertexArray(s_QuadVAO);
                glBindBuffer(GL_ARRAY_BUFFER, s_QuadVBO);
                glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
                glBindVertexArray(0);
            }
        }

        const glm::mat4& CaptureProjection()
        {
            static const glm::mat4 p = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
            return p;
        }

        const glm::mat4* CaptureViews()
        {
            static const glm::mat4 views[6] = {
                glm::lookAt(glm::vec3(0.0f), glm::vec3( 1, 0, 0), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0.0f), glm::vec3(-1, 0, 0), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 1, 0), glm::vec3(0,  0,  1)),
                glm::lookAt(glm::vec3(0.0f), glm::vec3( 0,-1, 0), glm::vec3(0,  0, -1)),
                glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 0, 1), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 0,-1), glm::vec3(0, -1,  0)) };
            return views;
        }

        int MipCount(uint32_t size)
        {
            int mips = 1;
            while (size > 1) { size >>= 1; ++mips; }
            return mips;
        }

        // GL küp yüzü + (u,v) ∈ [-1,1] -> yön (OpenGL küp harita kuralı)
        glm::vec3 CubeDirection(int face, float u, float v)
        {
            switch (face)
            {
                case 0: return glm::normalize(glm::vec3(1.0f, -v, -u));
                case 1: return glm::normalize(glm::vec3(-1.0f, -v, u));
                case 2: return glm::normalize(glm::vec3(u, 1.0f, v));
                case 3: return glm::normalize(glm::vec3(u, -1.0f, -v));
                case 4: return glm::normalize(glm::vec3(u, -v, 1.0f));
                default: return glm::normalize(glm::vec3(-u, -v, -1.0f));
            }
        }
    }

    bool ProceduralSkySettings::operator==(const ProceduralSkySettings& o) const
    {
        auto near3 = [](const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 1e-3f; };
        return near3(glm::normalize(SunDirection), glm::normalize(o.SunDirection)) && near3(SunColor, o.SunColor)
            && std::abs(SunIntensity - o.SunIntensity) < 1e-3f && near3(ZenithColor, o.ZenithColor)
            && near3(HorizonColor, o.HorizonColor) && near3(GroundColor, o.GroundColor)
            && std::abs(Turbidity - o.Turbidity) < 1e-3f && Physical == o.Physical
            && std::abs(Rayleigh - o.Rayleigh) < 1e-3f && std::abs(Mie - o.Mie) < 1e-3f && std::abs(MieAnisotropy - o.MieAnisotropy) < 1e-3f
            && std::abs(Ozone - o.Ozone) < 1e-3f && std::abs(SunDisk - o.SunDisk) < 1e-3f && near3(GroundAlbedo, o.GroundAlbedo);
    }

    EnvironmentMap::~EnvironmentMap()
    {
        if (m_Prefiltered)
            glDeleteTextures(1, &m_Prefiltered);
        if (m_OwnedSky)
            glDeleteTextures(1, &m_OwnedSky);
    }

    std::shared_ptr<EnvironmentMap> EnvironmentMap::FromCubemap(uint32_t cubemapID, uint32_t resolution)
    {
        if (!cubemapID || resolution == 0)
            return nullptr;

        EnsureGeometry();
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        // Kaynak mip zinciri: hem gökyüzü örneklemesi hem filtrelenmiş önem örneklemesi için
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapID);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        auto env = std::shared_ptr<EnvironmentMap>(new EnvironmentMap());
        env->ComputeSH(cubemapID, resolution);
        env->Prefilter(cubemapID, resolution);
        return env;
    }

    std::shared_ptr<EnvironmentMap> EnvironmentMap::FromProceduralSky(const ProceduralSkySettings& settings, uint32_t resolution)
    {
        EnsureGeometry();
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        if (!s_SkyShader)
            s_SkyShader = Shader::Create("Engine/src/Shaders/3D/IBL/ProceduralSky.cqsh");
        if (!s_SkyShader || !s_SkyShader->IsValid())
            return nullptr;

        uint32_t sky = 0;
        glGenTextures(1, &sky);
        glBindTexture(GL_TEXTURE_CUBE_MAP, sky);
        const int mips = MipCount(resolution);
        glTexStorage2D(GL_TEXTURE_CUBE_MAP, mips, GL_RGBA16F, (GLsizei)resolution, (GLsizei)resolution);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        {
            GLStateGuard guard;
            uint32_t fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, (GLsizei)resolution, (GLsizei)resolution);

            s_SkyShader->Bind();
            s_SkyShader->SetMat4("u_Projection", CaptureProjection());
            s_SkyShader->SetFloat3("u_SunDirection", glm::normalize(settings.SunDirection));
            s_SkyShader->SetFloat3("u_SunColor", settings.SunColor);
            s_SkyShader->SetFloat("u_SunIntensity", settings.SunIntensity);
            s_SkyShader->SetFloat3("u_ZenithColor", settings.ZenithColor);
            s_SkyShader->SetFloat3("u_HorizonColor", settings.HorizonColor);
            s_SkyShader->SetFloat3("u_GroundColor", settings.GroundColor);
            s_SkyShader->SetFloat("u_Turbidity", settings.Turbidity);
            s_SkyShader->SetInt("u_Physical", settings.Physical ? 1 : 0);
            s_SkyShader->SetFloat("u_Rayleigh", settings.Rayleigh);
            s_SkyShader->SetFloat("u_Mie", settings.Mie);
            s_SkyShader->SetFloat("u_MieG", std::clamp(settings.MieAnisotropy, 0.0f, 0.98f));
            s_SkyShader->SetFloat("u_Ozone", settings.Ozone);
            s_SkyShader->SetFloat("u_SunDiskScale", std::max(settings.SunDisk, 0.0f));
            s_SkyShader->SetFloat3("u_GroundAlbedo", settings.GroundAlbedo);

            glBindVertexArray(s_CubeVAO);
            for (int face = 0; face < 6; ++face)
            {
                s_SkyShader->SetMat4("u_View", CaptureViews()[face]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, sky, 0);
                glDrawArrays(GL_TRIANGLES, 0, 36);
            }
            glBindVertexArray(0);
            glDeleteFramebuffers(1, &fbo);
        }

        glBindTexture(GL_TEXTURE_CUBE_MAP, sky);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        auto env = std::shared_ptr<EnvironmentMap>(new EnvironmentMap());
        env->m_OwnedSky = sky;
        env->ComputeSH(sky, resolution);
        env->Prefilter(sky, resolution);
        return env;
    }

    void EnvironmentMap::ComputeSH(uint32_t cubemapID, uint32_t resolution)
    {
        // ~32px'lik mip seviyesini oku (difüz için fazlasıyla yeterli)
        int level = 0;
        uint32_t size = resolution;
        while (size > 32) { size >>= 1; ++level; }

        std::array<glm::vec3, 9> sh{};
        float weightSum = 0.0f;
        std::vector<float> pixels((size_t)size * size * 4);

        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapID);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        for (int face = 0; face < 6; ++face)
        {
            glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA, GL_FLOAT, pixels.data());
            for (uint32_t y = 0; y < size; ++y)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    float u = 2.0f * (x + 0.5f) / size - 1.0f;
                    float v = 2.0f * (y + 0.5f) / size - 1.0f;
                    float temp = 1.0f + u * u + v * v;
                    float weight = 4.0f / (std::sqrt(temp) * temp);
                    glm::vec3 d = CubeDirection(face, u, v);
                    const float* p = &pixels[((size_t)y * size + x) * 4];
                    glm::vec3 c(p[0], p[1], p[2]);
                    if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z))
                        continue;
                    c = glm::min(c, glm::vec3(64.0f)); // güneş gibi aşırı değerler SH'ı çınlatmasın

                    float basis[9] = {
                        0.282095f,
                        0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
                        1.092548f * d.x * d.y, 1.092548f * d.y * d.z,
                        0.315392f * (3.0f * d.z * d.z - 1.0f),
                        1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y) };
                    for (int i = 0; i < 9; ++i)
                        sh[i] += c * basis[i] * weight;
                    weightSum += weight;
                }
            }
        }

        if (weightSum <= 0.0f)
            return;
        const float norm = 4.0f * glm::pi<float>() / weightSum;
        // Kosinüs lobu konvolüsyonu (Ramamoorthi & Hanrahan), sonra /π
        const float band[9] = { glm::pi<float>(), 2.094395f, 2.094395f, 2.094395f, 0.785398f, 0.785398f, 0.785398f, 0.785398f, 0.785398f };
        for (int i = 0; i < 9; ++i)
            m_SH[i] = sh[i] * norm * band[i] / glm::pi<float>();
    }

    void EnvironmentMap::Prefilter(uint32_t cubemapID, uint32_t resolution)
    {
        if (!s_PrefilterShader)
            s_PrefilterShader = Shader::Create("Engine/src/Shaders/3D/IBL/Prefilter.cqsh");
        if (!s_PrefilterShader || !s_PrefilterShader->IsValid())
            return;

        const uint32_t size = std::clamp<uint32_t>(resolution, 32u, 256u);
        m_PrefilteredMips = std::min(MipCount(size), 7);

        glGenTextures(1, &m_Prefiltered);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_Prefiltered);
        glTexStorage2D(GL_TEXTURE_CUBE_MAP, m_PrefilteredMips, GL_RGBA16F, (GLsizei)size, (GLsizei)size);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, m_PrefilteredMips - 1);

        GLStateGuard guard;
        uint32_t fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        s_PrefilterShader->Bind();
        s_PrefilterShader->SetMat4("u_Projection", CaptureProjection());
        s_PrefilterShader->SetInt("u_Environment", 0);
        s_PrefilterShader->SetFloat("u_SourceResolution", (float)resolution);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapID);

        glBindVertexArray(s_CubeVAO);
        for (int mip = 0; mip < m_PrefilteredMips; ++mip)
        {
            uint32_t mipSize = std::max(1u, size >> mip);
            glViewport(0, 0, (GLsizei)mipSize, (GLsizei)mipSize);
            float roughness = (float)mip / (float)std::max(1, m_PrefilteredMips - 1);
            s_PrefilterShader->SetFloat("u_Roughness", roughness);
            s_PrefilterShader->SetInt("u_SampleCount", mip == 0 ? 1 : 128);
            for (int face = 0; face < 6; ++face)
            {
                s_PrefilterShader->SetMat4("u_View", CaptureViews()[face]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_Prefiltered, mip);
                glDrawArrays(GL_TRIANGLES, 0, 36);
            }
        }
        glBindVertexArray(0);
        glDeleteFramebuffers(1, &fbo);
    }

    uint32_t EnvironmentMap::GetBRDFLUT()
    {
        if (s_BRDFLUT)
            return s_BRDFLUT;

        EnsureGeometry();
        if (!s_BRDFShader)
            s_BRDFShader = Shader::Create("Engine/src/Shaders/3D/IBL/BRDFLut.cqsh");
        if (!s_BRDFShader || !s_BRDFShader->IsValid())
            return 0;

        constexpr int kSize = 256;
        glGenTextures(1, &s_BRDFLUT);
        glBindTexture(GL_TEXTURE_2D, s_BRDFLUT);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG16F, kSize, kSize);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GLStateGuard guard;
        uint32_t fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_BRDFLUT, 0);
        glViewport(0, 0, kSize, kSize);
        s_BRDFShader->Bind();
        glBindVertexArray(s_QuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDeleteFramebuffers(1, &fbo);
        return s_BRDFLUT;
    }

    void EnvironmentMap::Shutdown()
    {
        if (s_BRDFLUT) { glDeleteTextures(1, &s_BRDFLUT); s_BRDFLUT = 0; }
        if (s_CubeVAO) { glDeleteVertexArrays(1, &s_CubeVAO); glDeleteBuffers(1, &s_CubeVBO); s_CubeVAO = s_CubeVBO = 0; }
        if (s_QuadVAO) { glDeleteVertexArrays(1, &s_QuadVAO); glDeleteBuffers(1, &s_QuadVBO); s_QuadVAO = s_QuadVBO = 0; }
        s_PrefilterShader.reset();
        s_SkyShader.reset();
        s_BRDFShader.reset();
    }
}
