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
    Entity SpawnFromAsset(Scene* scene, const std::string& path, Entity parent = {}, const glm::vec3* worldPosition = nullptr);
    // Nesneye uygulanan varlıklar (yeni nesne oluşturmaz): .cqmat -> Mesh Renderer malzemesi, .cqphysmat -> çarpıştırıcı
    bool IsAppliedToEntity(const std::string& path);
    bool ApplyToEntity(Entity entity, const std::string& path);
    // Alt ağacı klasöre prefab olarak kaydet (benzersiz ad); kaynak varlık örneğe dönüşür. Yazılan yol döner (hata: boş)
    std::filesystem::path CreatePrefab(Entity root, const std::filesystem::path& directory);
}
