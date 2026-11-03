#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <memory>

namespace Conqueror
{
    class CQ_API Cubemap
    {
    public:
        virtual ~Cubemap() = default;

        virtual void Bind(uint32_t slot = 0) const = 0;
        virtual void Unbind() const = 0;

        virtual uint32_t GetRendererID() const = 0;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual const std::string& GetPath() const = 0;
        // Kaynak yol (ör. .cqcubemap): sahne bu yolu saklar, yeniden yüklerken aynı varlık okunur
        virtual void SetPath(const std::string& path) { (void)path; }

        // HDR equirectangular texture'dan cubemap oluştur; .cqcubemap varlığı da verilebilir (yüzler ya da panorama)
        static std::shared_ptr<Cubemap> CreateFromEquirectangular(const std::string& path, uint32_t resolution = 512);
        
        // 6 ayrı texture'dan cubemap oluştur
        static std::shared_ptr<Cubemap> CreateFromFaces(const std::string faces[6]);
    };
}
