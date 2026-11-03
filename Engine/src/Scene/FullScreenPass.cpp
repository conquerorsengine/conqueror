#include "FullScreenPass.h"
#include "Scene/ComponentRegistry.h"

#include <IconsFontAwesome6.h>

namespace Conqueror
{
    void RegisterFullScreenPassComponents()
    {
        using P = FullScreenPassComponent;
        ComponentDef<P>("Full Screen Pass", "FullScreenPassComponent", "Rendering", ICON_FA_WAND_MAGIC_SPARKLES).Color({ 0.75f, 0.55f, 1.0f, 1.0f })
            .Describe("Applies your own full-screen shader (Create > Shader > Image Effect Shader) to the camera image: "
                      "outlines, pixelation, scan lines, color effects. Active passes run in Order.")
            .Field("Shader", "Image Effect Shader", &P::Shader, FieldKind::Asset).Filter(".glsl;.frag")
            .Field("Intensity", "Intensity", &P::Intensity).Range(0.0f, 1.0f, 0.01f)
            .Field("Order", "Order", &P::Order).Range(-100.0f, 100.0f, 1.0f)
            .Field("Injection", "Injection Point", &P::Injection).Options({ "Before Post Processing (HDR)", "After Post Processing" });
    }
}
