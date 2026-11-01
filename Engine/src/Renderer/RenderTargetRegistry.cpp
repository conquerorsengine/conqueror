#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "RenderTargetRegistry.h"

#include <glad/glad.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    RenderTargetTexture::RenderTargetTexture(const std::string& name)
        : m_Name(name), m_Path("rt://" + name)
    {
        Resize(4, 4);
    }

    RenderTargetTexture::~RenderTargetTexture()
    {
        Release();
    }

    void RenderTargetTexture::Release()
    {
        if (m_Framebuffer)
            glDeleteFramebuffers(1, &m_Framebuffer);
        if (m_Texture)
            glDeleteTextures(1, &m_Texture);
        m_Framebuffer = m_Texture = 0;
        m_Width = m_Height = 0;
    }

    void RenderTargetTexture::Bind(uint32_t slot) const
    {
        if (m_Texture)
            glBindTextureUnit(slot, m_Texture);
    }

    void RenderTargetTexture::Resize(uint32_t width, uint32_t height)
    {
        width = std::clamp<uint32_t>(width, 1u, 8192u);
        height = std::clamp<uint32_t>(height, 1u, 8192u);
        if (width == m_Width && height == m_Height && m_Texture)
            return;
        Release();
        m_Width = width;
        m_Height = height;
        glGenTextures(1, &m_Texture);
        glBindTexture(GL_TEXTURE_2D, m_Texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLint previous = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
        glGenFramebuffers(1, &m_Framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Texture, 0);
        // Henüz çizilmemiş hedef siyah görünür (tanımsız bellek yerine)
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    }

    namespace
    {
        std::unordered_map<std::string, std::shared_ptr<RenderTargetTexture>>& Targets()
        {
            static std::unordered_map<std::string, std::shared_ptr<RenderTargetTexture>> s_Targets;
            return s_Targets;
        }
    }

    namespace RenderTargetRegistry
    {
        bool IsAsset(const std::string& path)
        {
            std::string ext = std::filesystem::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext == AssetExtension;
        }

        bool ReadAsset(const std::string& path, std::string& outName, int& outWidth, int& outHeight)
        {
            if (!IsAsset(path))
                return false;
            try
            {
                YAML::Node n = YAML::LoadFile(path)["RenderTexture"];
                if (!n)
                    return false;
                outName = std::filesystem::path(path).stem().string();
                outWidth = std::clamp(n["Width"].as<int>(512), 1, 4096);
                outHeight = std::clamp(n["Height"].as<int>(512), 1, 4096);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool WriteAsset(const std::string& path, int width, int height)
        {
            std::ofstream f(path, std::ios::trunc);
            if (!f)
                return false;
            f << "RenderTexture:\n  Width: " << std::clamp(width, 1, 4096) << "\n  Height: " << std::clamp(height, 1, 4096) << "\n";
            return true;
        }

        bool ParsePath(const std::string& path, std::string& outName)
        {
            // Proje yolu çözümlemesi öne dizin ekleyebilir: "…/Assets/rt://Ad"
            const size_t at = path.find("rt://");
            if (at == std::string::npos)
                return false;
            outName = path.substr(at + 5);
            while (!outName.empty() && (outName.back() == '/' || outName.back() == '\\'))
                outName.pop_back();
            return !outName.empty();
        }

        std::shared_ptr<RenderTargetTexture> Acquire(const std::string& nameOrPath)
        {
            std::string name = nameOrPath;
            ParsePath(nameOrPath, name);
            if (name.empty())
                return nullptr;
            auto& targets = Targets();
            auto it = targets.find(name);
            if (it != targets.end())
                return it->second;
            auto target = std::make_shared<RenderTargetTexture>(name);
            targets[name] = target;
            return target;
        }

        std::shared_ptr<RenderTargetTexture> Find(const std::string& nameOrPath)
        {
            std::string name = nameOrPath;
            ParsePath(nameOrPath, name);
            auto& targets = Targets();
            auto it = targets.find(name);
            return it != targets.end() ? it->second : nullptr;
        }

        void Clear()
        {
            Targets().clear();
        }
    }
}
