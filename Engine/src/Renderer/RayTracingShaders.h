#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Conqueror
{
    class Scene;

    // Ray Tracing Shader (Unity RayTracingShader): sahnenin üçgenlerine ışın gönderen shader. OpenGL'de donanım ışın
    // izleme hattı olmadığından compute shader üzerinde çalışır: sahnedeki Mesh Renderer'lardan BVH (sınırlayıcı hacim
    // ağacı) kurulur ve GPU'ya yüklenir. Dosya (.cqraytrace) yalnızca şu işlevi yazar:
    //     vec4 RayGeneration(ivec2 pixel, vec2 uv)
    // Hazır: bool CQ_TraceRay(vec3 origin, vec3 direction, float maxDistance, out CQ_RayHit hit),
    //        CQ_CameraRay(uv, out origin, out direction), u_SunDirection, u_SunColor, u_Time.
    // Betikten: RayTracingDispatch("Assets/Shadows.cqraytrace", "Assets/Result.cqrt") -> sonuç render texture'a yazılır.
    namespace RayTracingShaders
    {
        constexpr const char* Extension = ".cqraytrace";
        // Etkin sahnenin birincil kamerasından; hedef .cqrt render texture. Başarısızsa false (GetError)
        CQ_API bool Dispatch(Scene& scene, const std::string& shader, const std::string& targetTexture);
        CQ_API std::string GetError(const std::string& shader);
        // Son kurulan ivme yapısı (BVH) bilgisi
        CQ_API int GetTriangleCount();
        CQ_API int GetNodeCount();
        CQ_API void Shutdown();
    }
}
