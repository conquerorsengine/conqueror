#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Conqueror
{
    // Full Screen Pass (Unity URP Full Screen Pass / Image Effect Shader, HDRP Custom Pass): kendi yazdığınız tam ekran
    // shader'ı (Create > Shader > Image Effect Shader) sahne görüntüsüne uygular. Hacim gibi sahnede bir nesneye eklenir;
    // etkin olanlar Order sırasıyla çalışır.
    //
    // Shader dosyası (.glsl) yalnızca şu işlevi yazar:  vec4 Effect(vec2 uv)
    // Hazır değişkenler: sampler2D u_Source (HDR sahne rengi), sampler2D u_Depth, u_Time, u_Resolution, u_Intensity
    enum class FullScreenPassInjection : int { BeforePostProcess = 0, AfterPostProcess };

    struct FullScreenPassComponent
    {
        std::string Shader;            // proje göreli .glsl
        float Intensity = 1.0f;        // 0 = efekt yok, 1 = tam (kaynakla karışım)
        int Order = 0;
        FullScreenPassInjection Injection = FullScreenPassInjection::BeforePostProcess;
    };

    void RegisterFullScreenPassComponents();
}
