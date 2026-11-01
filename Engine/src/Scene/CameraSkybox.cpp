#include "CameraSkybox.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Cubemap.h"

#include <IconsFontAwesome6.h>

namespace Conqueror
{
    namespace CameraSkybox
    {
        std::shared_ptr<Cubemap> Resolve(CameraSkyboxComponent& c)
        {
            if (c.Skybox != c.LoadedSkybox)
            {
                c.LoadedSkybox = c.Skybox;
                c.Loaded = c.Skybox.empty() ? nullptr : Cubemap::CreateFromEquirectangular(SceneSerializer::ResolveAssetPath(c.Skybox), 512);
                if (c.Loaded && !c.Loaded->GetRendererID())
                    c.Loaded = nullptr;
            }
            return c.Loaded;
        }
    }

    void RegisterCameraSkyboxComponents()
    {
        using S = CameraSkyboxComponent;
        ComponentDef<S>("Skybox", "CameraSkyboxComponent", "Rendering", ICON_FA_CLOUD_SUN)
            .Describe("Gives this camera its own sky background (split screen, portals, menus). Lighting still uses the scene's sky.")
            .Field("Skybox", "Skybox", &S::Skybox, FieldKind::Asset).Filter(".hdr;.exr;.png;.jpg;.jpeg")
            .Field("Exposure", "Exposure", &S::Exposure).Range(0.0f, 16.0f, 0.01f)
            .Field("Tint", "Tint", &S::Tint, FieldKind::Color3);
    }
}
