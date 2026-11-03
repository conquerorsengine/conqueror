#include "OpenGLCubemap.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include "Core/Logging/Log.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/VertexArray.h"
#include "Renderer/RHI/Buffer.h"

#include <glad/glad.h>
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Conqueror
{
    OpenGLCubemap::OpenGLCubemap(const std::string& equirectangularPath, uint32_t resolution)
        : m_Width(resolution), m_Height(resolution), m_Path(equirectangularPath)
    {
        ConvertEquirectangularToCubemap(equirectangularPath, resolution);
    }

    OpenGLCubemap::OpenGLCubemap(const std::string faces[6])
    {
        // Yol: ilk yüz (Cubemap varlığından gelince SetPath ile .cqcubemap olur); nullptr: boş küp (yansıma yoklaması)
        m_Path = faces ? faces[0] : std::string();
        LoadFromFaces(faces);
    }

    OpenGLCubemap::~OpenGLCubemap()
    {
        glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLCubemap::Bind(uint32_t slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
    }

    void OpenGLCubemap::Unbind() const
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void OpenGLCubemap::ConvertEquirectangularToCubemap(const std::string& path, uint32_t resolution)
    {
        // HDR texture yükle
        stbi_set_flip_vertically_on_load(true);
        int width, height, channels;
        // Her zaman 3 kanal iste: alfa kanallı (RGBA) PNG/JPG'ler GL_RGB yüklemesinde satırları kaydırıyordu
        float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 3);

        if (!data)
        {
            CQ_CORE_ERROR("Failed to load HDR image: {0}", path);
            return;
        }

        CQ_CORE_INFO("Loaded HDR: {0} ({1}x{2}, {3} channels)", path, width, height, channels);

        // HDR gökyüzleri göreli birimlerle gelir (ortalama 0.5 de olabilir 50 de). Unreal bunu otomatik
        // pozlamayla çözer; burada yüklerken alan ağırlıklı medyan parlaklık ~0.45 olacak şekilde ölçeklenir
        // (0.25 ufku düzeltiyordu ama gökyüzünü fazla karartıyordu).
        // Gökyüzü ve ondan üretilen ortam ışığı birlikte ölçeklendiği için sahne tutarlı kalır; ince ayar
        // Lighting panelindeki Skybox Exposure ile yapılır. LDR (PNG/JPG) görüntüler olduğu gibi kalır.
        if (stbi_is_hdr(path.c_str()) && width > 0 && height > 0)
        {
            const int step = std::max(1, (int)std::sqrt((double)width * height / 200000.0));
            std::vector<std::pair<float, float>> samples;   // (parlaklık, alan ağırlığı)
            samples.reserve((size_t)(width / step + 1) * (height / step + 1));
            double totalWeight = 0.0;
            for (int y = 0; y < height; y += step)
            {
                const float latitude = ((y + 0.5f) / height - 0.5f) * 3.14159265f;
                const float w = std::cos(latitude);
                for (int x = 0; x < width; x += step)
                {
                    const float* px = data + ((size_t)y * width + x) * 3;
                    const float lum = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
                    if (std::isfinite(lum))
                    {
                        samples.emplace_back(lum, w);
                        totalWeight += w;
                    }
                }
            }
            std::sort(samples.begin(), samples.end());
            double acc = 0.0;
            float median = 0.0f;
            for (const auto& [lum, w] : samples)
            {
                acc += w;
                if (acc >= totalWeight * 0.5) { median = lum; break; }
            }
            if (median > 1e-6f)
            {
                m_SourceScale = std::clamp(0.45f / median, 1e-4f, 1e4f);
                const size_t count = (size_t)width * height * 3;
                for (size_t i = 0; i < count; ++i)
                    data[i] *= m_SourceScale;
                CQ_CORE_INFO("Skybox normalized: median luminance {0:.3f} -> x{1:.3f}", median, m_SourceScale);
            }
        }

        // RGB16F en çok 65504 tutar: güneşli HDR'lerde (puresky) güneş pikselleri sonsuza taşar, filtrelenmiş ortam
        // ışığı NaN olur ve tüm sahne siyah görünür. Değerler sınırlanır (güneş yine çok parlak kalır), NaN atılır.
        {
            const size_t count = (size_t)width * height * 3;
            for (size_t i = 0; i < count; ++i)
                data[i] = std::isfinite(data[i]) ? std::clamp(data[i], 0.0f, 20000.0f) : 0.0f;
        }

        // Equirectangular texture oluştur
        uint32_t hdrTexture;
        glGenTextures(1, &hdrTexture);
        glBindTexture(GL_TEXTURE_2D, hdrTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);

        // Cubemap oluştur
        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
        for (uint32_t i = 0; i < 6; i++)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, resolution, resolution, 0, GL_RGB, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Projection ve view matrixleri
        glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        glm::mat4 captureViews[] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
        };

        // Conversion shader yükle
        auto conversionShader = Shader::Create("Engine/src/Shaders/3D/EquirectangularToCubemap.cqsh");
        conversionShader->Bind();
        conversionShader->SetInt("u_EquirectangularMap", 0);
        conversionShader->SetMat4("u_Projection", captureProjection);

        // Cube mesh oluştur
        float cubeVertices[] = {
            -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f
        };

        uint32_t cubeVAO, cubeVBO;
        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        // Framebuffer oluştur
        uint32_t captureFBO, captureRBO;
        glGenFramebuffers(1, &captureFBO);
        glGenRenderbuffers(1, &captureRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, resolution, resolution);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

        // Equirectangular'ı cubemap'e convert et
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrTexture);

        glViewport(0, 0, resolution, resolution);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        for (uint32_t i = 0; i < 6; i++)
        {
            conversionShader->SetMat4("u_View", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_RendererID, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glBindVertexArray(cubeVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Cleanup
        glDeleteTextures(1, &hdrTexture);
        glDeleteVertexArrays(1, &cubeVAO);
        glDeleteBuffers(1, &cubeVBO);
        glDeleteFramebuffers(1, &captureFBO);
        glDeleteRenderbuffers(1, &captureRBO);

        CQ_CORE_INFO("Cubemap created from HDR: {0}x{1}", resolution, resolution);
    }

    void OpenGLCubemap::LoadFromFaces(const std::string faces[6])
    {
        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        if (!faces)
        {
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            return;
        }

        // Yüzler: HDR (float) ya da LDR (sRGB); boyutlar farklıysa en büyük kare boyuta ölçeklenir, eksik yüz gri olur
        struct Face
        {
            std::vector<float> Hdr;
            std::vector<unsigned char> Ldr;
            int W = 0, H = 0;
        };
        Face loaded[6];
        int size = 0;
        for (int i = 0; i < 6; ++i)
        {
            if (faces[i].empty())
                continue;
            int w = 0, h = 0, c = 0;
            if (stbi_is_hdr(faces[i].c_str()))
            {
                if (float* data = stbi_loadf(faces[i].c_str(), &w, &h, &c, 4))
                {
                    loaded[i].Hdr.assign(data, data + (size_t)w * h * 4);
                    stbi_image_free(data);
                }
            }
            else if (unsigned char* data = stbi_load(faces[i].c_str(), &w, &h, &c, 4))
            {
                loaded[i].Ldr.assign(data, data + (size_t)w * h * 4);
                stbi_image_free(data);
            }
            if (w <= 0 || h <= 0)
            {
                CQ_CORE_ERROR("Cubemap face failed to load: {0}", faces[i]);
                continue;
            }
            loaded[i].W = w;
            loaded[i].H = h;
            size = std::max(size, std::max(w, h));
        }
        size = std::clamp(size > 0 ? size : 4, 4, 4096);
        m_Width = m_Height = (uint32_t)size;
        for (int i = 0; i < 6; ++i)
        {
            Face& f = loaded[i];
            std::vector<float> rgba((size_t)size * size * 4, 0.5f);
            for (size_t k = 3; k < rgba.size(); k += 4)
                rgba[k] = 1.0f;
            if (f.W > 0)
            {
                std::vector<float> src((size_t)f.W * f.H * 4);
                if (!f.Hdr.empty())
                    src = f.Hdr;
                else
                    for (size_t k = 0; k < src.size(); ++k)
                    {
                        // sRGB -> doğrusal (HDR akışında gökyüzü doğru parlaklıkta)
                        const float v = f.Ldr[k] / 255.0f;
                        src[k] = (k % 4 == 3) ? v : (v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f));
                    }
                if (f.W == size && f.H == size)
                    rgba = std::move(src);
                else
                    stbir_resize_float_linear(src.data(), f.W, f.H, 0, rgba.data(), size, size, 0, STBIR_RGBA);
            }
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, rgba.data());
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    }
}
