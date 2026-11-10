#pragma once

#include "Scene/Entity.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor::AssetActions
{
    // İçerik Tarayıcısından sürüklenen varlık sahneye bırakılabilir mi (prefab, model, doku, ses, betik)
    bool CanSpawn(const std::string& path);
    // Varlığı sahneye yerleştir: prefab örneği, model, sprite, ses kaynağı; betik hedef varlığa eklenir.
    // parent: ebeveyn (betikte hedef). worldPosition: kök için dünya konumu (yoksa varsayılan).
    // spriteSlice: görselin bir dilimi (İçerik Tarayıcısı > oku); boşsa tüm görsel
    Entity SpawnFromAsset(Scene* scene, const std::string& path, Entity parent = {}, const glm::vec3* worldPosition = nullptr, const std::string& spriteSlice = {});
    // Görsel ilk kez sprite olarak kullanılıyorsa içe aktarma türü Sprite olur (mipmapsız, bulanmaz); ayarı olan görsele dokunulmaz
    void EnsureSpriteImport(const std::string& path);
    // Nesneye uygulanan varlıklar (yeni nesne oluşturmaz): .cqmat -> Mesh Renderer malzemesi, .cqphysmat -> çarpıştırıcı
    bool IsAppliedToEntity(const std::string& path);
    bool ApplyToEntity(Entity entity, const std::string& path);
    // Betik yalnız Data Asset sınıfı tanımlıyorsa nesneye eklenmez: Unity'nin "Can't add script" penceresi gösterilir, true döner
    bool RefuseDataScript(const std::string& scriptPath);
    // Menüden oluşturulan 3B ilkele şekline uygun çarpıştırıcı ekler (Unity gibi): küp Box, küre Sphere, düzlem Mesh; silindir Convex Mesh
    // (bizim silindirde yükseklik = çap: Capsule tam küre olurdu, üstündeki nesneler yuvarlanırdı)
    void AddPrimitiveCollider(Entity entity);
    // Alt ağacı klasöre prefab olarak kaydet (benzersiz ad); kaynak varlık örneğe dönüşür. Yazılan yol döner (hata: boş)
    std::filesystem::path CreatePrefab(Entity root, const std::filesystem::path& directory);
}
