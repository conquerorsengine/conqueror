#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/TextureSettings.h"

#include <string>
#include <unordered_map>
#include <memory>

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

        // Ayrıntılar (doku denetçisi / istatistikler)
        virtual uint32_t GetMipLevels() const { return 1; }
        virtual uint64_t GetMemoryBytes() const { return 0; }
        virtual std::string GetFormatName() const { return {}; }
        virtual uint32_t GetSourceWidth() const { return GetWidth(); }
        virtual uint32_t GetSourceHeight() const { return GetHeight(); }
        virtual uint32_t GetSourceChannels() const { return 4; }
        virtual const TextureImportSettings& GetImportSettings() const
        {
            static const TextureImportSettings s_Default;
            return s_Default;
        }
        // Dosyadan ve .meta ayarlarından yerinde yeniden yükle (aynı nesne, yeni GPU dokusu)
        virtual bool Reload() { return false; }
        // Kaynak dosya silindi: içerik saydam 1x1 olur (Unity'de referansı kayıp doku gibi görünmez), yol korunur
        virtual void SetMissing() {}
        // Texture Streaming (Unity Mipmap Streaming, içe aktarmada Streaming Mipmaps): GPU'daki en ayrıntılı mip seviyesi
        // (0 = tam boyut) ve dokunun en kaba seviyesi. Genişlik / yükseklik her zaman tam boyuttur
        virtual bool IsStreamed() const { return false; }
        virtual int GetStreamingLevel() const { return 0; }
        virtual int GetStreamingMaxLevel() const { return 0; }
        virtual bool StreamDropTo(int level) { (void)level; return false; }
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

        static void ClearCache() { s_Cache.clear(); }
        // Önbellekteki (yüklü) dokuyu yerinde yenile: içe aktarma ayarları değişince tüm kullanıcılar güncellenir
        static bool ReloadFromDisk(const std::string& path);
        // Yüklü dokuların kaynak dosyalarını denetler (editör çağırır): değişen dosya yerinde yeniden yüklenir, silinen
        // dosyanın dokusu boşalır, geri gelen dosya yeniden yüklenir. Değişen doku sayısı
        static int CheckFileChanges();
        static std::unordered_map<std::string, std::weak_ptr<Texture2D>>& GetCache() { return s_Cache; }

    private:
        static inline std::unordered_map<std::string, std::weak_ptr<Texture2D>> s_Cache;
    };
}
