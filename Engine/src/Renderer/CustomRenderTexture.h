#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Conqueror
{
    class RenderTargetTexture;

    // Custom Render Texture (Unity Custom Render Texture): içeriği bir shader tarafından (her karede, açılışta ya da
    // istenince) güncellenen doku. Önceki kare shader'a verilir (çift tampon): su dalgaları, yanma, simülasyon, prosedürel
    // desen. Her doku yuvasına sürüklenir (malzeme, sprite, UI, Billboard ...).
    //
    // Shader dosyası (.glsl) yalnızca şu işlevi yazar:  vec4 Update(vec2 uv)
    // Hazır değişkenler: u_Time, u_DeltaTime, u_Resolution, u_FrameCount, sampler2D u_SelfTexture (önceki kare),
    // sampler2D u_InitTexture.
    struct CustomRenderTextureAsset
    {
        int Width = 256, Height = 256;
        std::string Shader;            // proje göreli .glsl
        int UpdateMode = 1;            // 0 OnLoad, 1 Realtime, 2 OnDemand
        float UpdatePeriod = 0.0f;     // Realtime: saniye (0 = her kare)
        glm::vec4 InitColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        std::string InitTexture;       // boşsa InitColor
        bool DoubleBuffered = true;
        bool Repeat = false;           // doku sarma (kenarlar)
    };

    namespace CustomRenderTextures
    {
        constexpr const char* Extension = ".cqcrt";
        CQ_API bool IsAsset(const std::string& path);
        CQ_API bool Load(const std::string& path, CustomRenderTextureAsset& out);
        CQ_API bool Save(const std::string& path, const CustomRenderTextureAsset& asset);
        // Dokuyu al (ilk kez: oluşturulur ve başlangıç değerine getirilir)
        CQ_API std::shared_ptr<RenderTargetTexture> Acquire(const std::string& path);
        // Her karede bir kez (sahne çizimi başında): Realtime / OnLoad / istenen güncellemeler
        CQ_API void UpdateAll();
        // OnDemand: bir sonraki karede güncelle; initialize = başlangıç değerine dön
        CQ_API void RequestUpdate(const std::string& path, bool initialize = false);
        // Son derleme hatası (Inspector gösterir)
        CQ_API std::string GetError(const std::string& path);
        CQ_API uint64_t GetUpdateCount(const std::string& path);
        CQ_API void Shutdown();
    }
}
