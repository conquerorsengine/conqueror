#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Conqueror
{
    class CQ_API Texture
    {
    public:
        virtual ~Texture() = default;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetRendererID() const = 0;
        virtual const std::string& GetPath() const = 0;

        virtual void SetData(void* data, uint32_t size) = 0;

        virtual void Bind(uint32_t slot = 0) const = 0;

        virtual bool operator==(const Texture& other) const = 0;
    };

    class CQ_API Texture2D : public Texture
    {
    public:
        static std::shared_ptr<Texture2D> Create(uint32_t width, uint32_t height);
        static std::shared_ptr<Texture2D> Create(const std::string& path);
        /// stb destekleyen sıkıştırılmış görüntü tamponu (FBX/Assimp gömülü)
        static std::shared_ptr<Texture2D> CreateFromCompressedImageMemory(const unsigned char* data, uint32_t byteSize);
        /// Assimp sıkıştırılmamış gömülü doku (BGRA)
        static std::shared_ptr<Texture2D> CreateFromRawBGRA(uint32_t width, uint32_t height, const unsigned char* bgraPixels);
    };
}
