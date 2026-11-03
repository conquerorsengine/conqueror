#include "Renderer/CustomRenderTexture.h"
#include "Texture.h"
#include "Core/Utils/ImportActivity.h"
#include "Renderer/RenderTargetRegistry.h"
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include "Renderer/Renderer.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLTexture.h"

namespace Conqueror
{
    namespace
    {
        void Watch(const std::shared_ptr<Texture2D>& tex);
    }

    std::shared_ptr<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(width, height);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Texture2D> Texture2D::Create(const std::string& path)
    {
        // "rt://Ad": kamera render hedefi (SubViewport)
        if (std::string target; RenderTargetRegistry::ParsePath(path, target))
            return RenderTargetRegistry::Acquire(target);
        // Custom Render Texture (.cqcrt): shader'ın güncellediği doku
        if (CustomRenderTextures::IsAsset(path))
            return CustomRenderTextures::Acquire(path);
        // Render Texture varlığı (.cqrt): aynı adlı render hedefi, dosyadaki boyutta
        if (RenderTargetRegistry::IsAsset(path))
        {
            std::string name;
            int w = 0, h = 0;
            if (!RenderTargetRegistry::ReadAsset(path, name, w, h))
                return nullptr;
            auto rt = RenderTargetRegistry::Acquire(name);
            if (rt && rt->GetWidth() <= 4 && rt->GetHeight() <= 4)
                rt->Resize((uint32_t)w, (uint32_t)h);
            return rt;
        }
        auto it = s_Cache.find(path);
        if (it != s_Cache.end())
        {
            if (auto cached = it->second.lock())
                return cached;
        }

        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:
            {
                const auto t0 = std::chrono::steady_clock::now();
                auto tex = std::make_shared<OpenGLTexture2D>(path);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                if (!tex->GetRendererID())
                {
                    ImportActivity::Add(path, "Texture", ms, false, "could not be read");
                    return nullptr;
                }
                ImportActivity::Add(path, "Texture", ms, true, std::to_string(tex->GetWidth()) + "x" + std::to_string(tex->GetHeight()));
                s_Cache[path] = tex;
                Watch(tex);
                return tex;
            }
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    namespace
    {
        // Yüklenen dokuların kaynak dosyaları (çözülmüş yol -> son bilinen yazma zamanı)
        struct WatchedFile
        {
            std::weak_ptr<Texture2D> Texture;
            std::filesystem::file_time_type Time{};
            bool Missing = false;
        };
        std::unordered_map<std::string, WatchedFile>& Watched()
        {
            static std::unordered_map<std::string, WatchedFile> s;
            return s;
        }
        void Watch(const std::shared_ptr<Texture2D>& tex)
        {
            if (!tex || tex->GetPath().empty())
                return;
            std::error_code ec;
            auto& w = Watched()[tex->GetPath()];
            w.Texture = tex;
            w.Time = std::filesystem::last_write_time(tex->GetPath(), ec);
            w.Missing = false;
        }
    }

    int Texture2D::CheckFileChanges()
    {
        int changed = 0;
        auto& watched = Watched();
        for (auto it = watched.begin(); it != watched.end();)
        {
            auto tex = it->second.Texture.lock();
            if (!tex)
            {
                it = watched.erase(it);
                continue;
            }
            std::error_code ec;
            const bool exists = std::filesystem::exists(it->first, ec);
            if (!exists)
            {
                if (!it->second.Missing)
                {
                    tex->SetMissing();
                    it->second.Missing = true;
                    ++changed;
                }
            }
            else
            {
                const auto time = std::filesystem::last_write_time(it->first, ec);
                if (!ec && (it->second.Missing || time != it->second.Time))
                {
                    // Dosya yazılırken okunmasın: zaman bir sonraki denetimde de aynıysa yükle
                    if (!it->second.Missing && time != it->second.Time && std::chrono::file_clock::now() - time < std::chrono::milliseconds(300))
                    {
                        ++it;
                        continue;
                    }
                    const auto t0 = std::chrono::steady_clock::now();
                    const bool reloaded = tex->Reload();
                    ImportActivity::Add(it->first, "Texture", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                                        reloaded, reloaded ? std::to_string(tex->GetWidth()) + "x" + std::to_string(tex->GetHeight()) : "could not be read", true);
                    if (reloaded)
                    {
                        it->second.Time = time;
                        it->second.Missing = false;
                        ++changed;
                    }
                }
            }
            ++it;
        }
        return changed;
    }

    bool Texture2D::ReloadFromDisk(const std::string& path)
    {
        std::error_code ec;
        const std::string canonical = std::filesystem::weakly_canonical(path, ec).string();
        bool any = false;
        for (auto& [key, weak] : s_Cache)
        {
            auto tex = weak.lock();
            if (!tex)
                continue;
            if (tex->GetPath() == canonical || tex->GetPath() == path)
            {
                const auto t0 = std::chrono::steady_clock::now();
                const bool reloaded = tex->Reload();
                ImportActivity::Add(tex->GetPath(), "Texture", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                                    reloaded, std::to_string(tex->GetWidth()) + "x" + std::to_string(tex->GetHeight()), true);
                any |= reloaded;
            }
        }
        return any;
    }

    std::shared_ptr<Texture2D> Texture2D::CreateFromCompressedImageMemory(const unsigned char* data, uint32_t byteSize)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:
            {
                auto tex = std::make_shared<OpenGLTexture2D>(data, byteSize);
                if (!tex->GetRendererID())
                    return nullptr;
                return tex;
            }
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Texture2D> Texture2D::CreateFromRawBGRA(uint32_t width, uint32_t height, const unsigned char* bgraPixels)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:
            {
                if (!width || !height || !bgraPixels)
                    return nullptr;

                auto tex = std::make_shared<OpenGLTexture2D>(width, height, bgraPixels);
                if (!tex->GetRendererID())
                    return nullptr;
                return tex;
            }
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
