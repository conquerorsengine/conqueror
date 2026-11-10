#pragma once

#include <string>

// Create > Shader şablonları
namespace Conqueror::Editor::ShaderTemplates
{
    std::string Lit();                  // .cqsh: yerleşik PBR + kullanıcı yüzey işlevi (Unity Standard Surface Shader)
    std::string Unlit();                // .cqsh: ışıksız
    std::string UI();                   // .cqsh: UI Image / Raw Image > Material (Position As UV1 -> v_UV1)
    std::string ImageEffect();          // .glsl: Full Screen Pass (Unity Image Effect Shader)
    std::string CustomRenderTexture();  // .glsl: Custom Render Texture güncellemesi
    std::string Compute();              // .comp: GPU hesap (betikten ComputeDispatch)
    std::string RayTracing();           // .cqraytrace: sahneye ışın (betikten RayTracingDispatch)
}
