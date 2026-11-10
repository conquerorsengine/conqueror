#pragma once

#include "Core/Base/Base.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>

// Unity 2D Animation paketi karşılıkları:
// - Sprite Library: .cqspritelib varlığı (kategori + etiket -> sprite); bir nesneye eklenir, altındakiler kullanır
// - Sprite Resolver: aynı nesnedeki Sprite Renderer'ın görselini kütüphaneden kategori / etiketle seçer (kostüm, yüz ifadesi,
//   kare kare animasyon: Label Index canlandırılabilir)
// - Sprite Skin: tek bir sprite'ı 2D kemiklerle (Bone 2D) büker; sprite bir ızgara ağa bölünür, her köşe en yakın kemiklere
//   ağırlıklarla bağlanır (Bind Bones), kemikler dönünce ağ deforme olur
namespace Conqueror
{
    class Scene;
    struct SpriteRendererComponent;

    struct SpriteLibraryComponent
    {
        std::string Asset;                    // .cqspritelib
    };

    struct SpriteResolverComponent
    {
        std::string Category;
        std::string Label;
        int LabelIndex = -1;                  // >= 0: kategorideki o sıradaki etiket (animasyon için); -1: Label alanı
        // Çalışma zamanı
        int AppliedIndex = -1;
        std::string AppliedKey;
    };

    struct SpriteSkinComponent
    {
        uint64_t RootBone = 0;                // kemik hiyerarşisinin kökü (Bone 2D)
        int GridX = 6, GridY = 6;             // ağ hücreleri (Bind Bones'ta)
        // Bağlama verisi (Bind Bones): kemikler kökten ad yoluyla (prefab kopyası kendi kemiklerini bulur)
        std::vector<std::string> BonePaths;
        std::vector<glm::mat4> BindInverse;   // bağlamadaki inverse(kemik dünya) * sprite dünya
        std::vector<glm::vec2> Vertices;      // sprite yerel
        std::vector<glm::vec2> UVs;           // sprite dikdörtgeni içindeki oran (0..1)
        std::vector<glm::ivec4> BoneIndex;    // köşe başına en çok 4 kemik (-1 yok)
        std::vector<glm::vec4> BoneWeight;
        int BoundGridX = 0, BoundGridY = 0;
        // Bilgi
        int BoundBones = 0;
        int BoundVertices = 0;
    };

    namespace SpriteAnimation2D
    {
        // Sprite Resolver'lar (düzenleyicide ve oyunda her kare)
        CQ_API void Update(Scene& scene);
        // Sprite Skin bağlıysa deforme ağı çizer (true); değilse çağıran normal sprite çizer
        CQ_API bool DrawSkinned(Scene& scene, entt::entity entity, SpriteRendererComponent& sprite, const glm::vec4& color);
        // Şimdiki pozda kemikleri bağla: ızgara ağ + otomatik ağırlıklar
        CQ_API bool Bind(Scene& scene, entt::entity entity, std::string& error);
        CQ_API void Unbind(SpriteSkinComponent& skin);
        // Kütüphane yardımcıları (Inspector açılır listeleri)
        CQ_API std::string FindLibraryAsset(Scene& scene, entt::entity entity);
        CQ_API std::vector<std::string> Categories(const std::string& libraryAsset);
        CQ_API std::vector<std::string> Labels(const std::string& libraryAsset, const std::string& category);
    }

    void RegisterSpriteAnimation2DComponents();
}
