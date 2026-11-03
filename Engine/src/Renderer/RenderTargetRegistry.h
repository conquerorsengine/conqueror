#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Texture.h"

#include <memory>
#include <string>

// Kamera render hedefleri (Godot SubViewport / Unity RenderTexture): "rt://Ad" doku yolu her doku alanında
// (Mesh Renderer, Sprite, UI Raw Image, malzeme, çıkartma) bu dokuyu verir. Doku nesnesi sabittir; boyut
// değişince içindeki GL dokusu yenilenir, böylece onu tutan tüm kullanıcılar güncel kalır.
namespace Conqueror
{
    class CQ_API RenderTargetTexture : public Texture2D
    {
    public:
        explicit RenderTargetTexture(const std::string& name);
        ~RenderTargetTexture() override;

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetRendererID() const override { return m_Texture; }
        const std::string& GetPath() const override { return m_Path; }
        void SetData(void*, uint32_t) override {}
        void Bind(uint32_t slot = 0) const override;
        bool operator==(const Texture& other) const override { return GetRendererID() == other.GetRendererID(); }
        uint64_t GetMemoryBytes() const override { return (uint64_t)m_Width * m_Height * 4; }
        std::string GetFormatName() const override { return "RGBA8 (render target)"; }

        const std::string& GetName() const { return m_Name; }
        // Varlıktan gelen hedeflerde yol = varlık dosyası (kaydedilen sahne / malzeme yeniden açınca varlığı yükler)
        void SetPath(const std::string& path) { m_Path = path; }
        // Boyut farklıysa yeniden ayır (içerik siyaha döner)
        void Resize(uint32_t width, uint32_t height);
        // Bu dokuya çizen çerçeve tamponu (tonlama çözümü buraya yazar)
        uint32_t GetFramebuffer() const { return m_Framebuffer; }
        // Kaç kez çizildi (otomasyon / tanılama)
        uint64_t RenderCount = 0;

    private:
        void Release();

        std::string m_Name, m_Path;
        uint32_t m_Texture = 0, m_Framebuffer = 0;
        uint32_t m_Width = 0, m_Height = 0;
    };

    namespace RenderTargetRegistry
    {
        // "rt://Ad" ya da yalnızca "Ad"; yoksa 4x4 siyah olarak oluşturulur (kamera ilk çizimde boyutlandırır)
        CQ_API std::shared_ptr<RenderTargetTexture> Acquire(const std::string& name);
        CQ_API std::shared_ptr<RenderTargetTexture> Find(const std::string& name);
        // Yol bir render hedefini gösteriyor mu ("…rt://Ad"), gösteriyorsa ad
        CQ_API bool ParsePath(const std::string& path, std::string& outName);
        CQ_API void Clear();

        // Render Texture varlığı (.cqrt, Unity Render Texture): ad = dosya adı, boyut dosyada. Her doku yuvasına ve
        // Render Target bileşeninin "Target Texture" yuvasına sürüklenir.
        constexpr const char* AssetExtension = ".cqrt";
        CQ_API bool IsAsset(const std::string& path);
        CQ_API bool ReadAsset(const std::string& path, std::string& outName, int& outWidth, int& outHeight);
        CQ_API bool WriteAsset(const std::string& path, int width, int height);
    }
}
