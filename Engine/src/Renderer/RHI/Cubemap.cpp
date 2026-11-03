#include "Cubemap.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/CubemapAsset.h"
#include "Renderer/Renderer.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLCubemap.h"

namespace Conqueror
{
    std::shared_ptr<Cubemap> Cubemap::CreateFromEquirectangular(const std::string& path, uint32_t resolution)
    {
        // Cubemap varlığı: altı yüz ya da panorama; yol varlığın kendisi olarak kalır
        if (CubemapAssets::IsAsset(path))
        {
            CubemapAssetData data;
            std::string error;
            if (!CubemapAssets::Load(path, data, &error))
            {
                CQ_CORE_ERROR("Cubemap {0}: {1}", path, error);
                return nullptr;
            }
            std::shared_ptr<Cubemap> cube;
            if (data.Source == CubemapAssetData::SourceKind::Panorama)
            {
                if (data.Panorama.empty())
                    return nullptr;
                cube = CreateFromEquirectangular(SceneSerializer::ResolveAssetPath(data.Panorama), (uint32_t)data.Resolution);
            }
            else
            {
                std::string faces[6];
                for (int i = 0; i < 6; ++i)
                    faces[i] = data.Faces[i].empty() ? std::string() : SceneSerializer::ResolveAssetPath(data.Faces[i]);
                cube = CreateFromFaces(faces);
            }
            if (cube)
                cube->SetPath(SceneSerializer::ResolveAssetPath(path));
            return cube;
        }
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:
                CQ_CORE_ASSERT(false, "RendererAPI::None is not supported!");
                return nullptr;
            case RendererAPI::API::OpenGL:
                return std::make_shared<OpenGLCubemap>(path, resolution);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Cubemap> Cubemap::CreateFromFaces(const std::string faces[6])
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:
                CQ_CORE_ASSERT(false, "RendererAPI::None is not supported!");
                return nullptr;
            case RendererAPI::API::OpenGL:
                return std::make_shared<OpenGLCubemap>(faces);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
