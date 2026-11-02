#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace Conqueror
{
    class Material;
    class Scene;
    class Texture2D;
    struct MeshRendererComponent;

    // Mesh Renderer'ın malzemesine erişim (Unity renderer.sharedMaterial / renderer.material)
    namespace MeshRendererMaterial
    {
        // Çizimde kullanılan malzeme (atanmamışsa paketteki Default Grid). Asla nullptr değil.
        CQ_API const Material& Shared(const MeshRendererComponent& renderer);

        // Değiştirilecek malzeme: paylaşılan bir varlıksa (.cqmat, paket malzemesi) ya da başka nesnelerle ortaksa bu
        // nesne için "<ad> (Instance)" kopyası çıkarılır ve ona yazılır (Unity renderer.material). Dosya değişmez.
        CQ_API Material& Edit(MeshRendererComponent& renderer);

        // Eski Mesh Renderer alanlarından (renk, doku, normal / maske haritası, ışıma, döşeme, kırpma, unlit) malzeme
        // (eski sahneler, betiklerin eski alan adları)
        struct LegacyFields
        {
            glm::vec4 Color{ 1.0f };
            float Metallic = 0.0f, Roughness = 0.5f, AO = 1.0f;
            glm::vec3 EmissionColor{ 0.0f };
            float EmissionStrength = 0.0f;
            bool EmissionFromTexture = false;
            glm::vec2 Tiling{ 1.0f }, Offset{ 0.0f };
            bool WorldAlignedUV = false, AlignToObject = false;
            std::shared_ptr<Texture2D> Texture, NormalMap, MaskMap;
            bool Unlit = false, AlphaClip = false;
            float AlphaClipThreshold = 0.5f;
        };
        CQ_API void Apply(Material& material, const LegacyFields& fields);

        // Kısayollar (betikler / tween'ler / araçlar)
        CQ_API glm::vec4 GetColor(const MeshRendererComponent& renderer);
        CQ_API void SetColor(MeshRendererComponent& renderer, const glm::vec4& color);
        CQ_API void SetTexture(MeshRendererComponent& renderer, const std::shared_ptr<Texture2D>& texture);

        // Sahnedeki gömülü (Instance) malzemeleri içerik bazında tekilleştirip <dir>/*.cqmat dosyalarına yazar ve
        // Mesh Renderer'lara atar (eski sahne / şablon taşıma). relativeTo verilirse (etkin proje yokken, ör. Hub)
        // MaterialPath o klasöre göreli yazılır. Atanan renderer sayısı; created = yeni dosya sayısı.
        CQ_API int BakeInstances(Scene& scene, const std::filesystem::path& dir, int* created = nullptr,
                                 const std::filesystem::path& relativeTo = {});
    }
}
