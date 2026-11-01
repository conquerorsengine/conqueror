#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Conqueror
{
    class Material;
    struct MeshRendererComponent;
}

namespace Conqueror::Editor::MaterialEditorUI
{
    // Unity Lit malzeme denetçisi (Surface Options / Surface Inputs / Advanced): .cqmat varlığı, Content Browser'da
    // seçili malzeme ve Mesh Renderer'ın altındaki malzeme bölümü aynı arayüzü kullanır. Değişince true.
    struct Options
    {
        bool Embedded = false;   // Mesh Renderer'ın gömülü malzemesi: ayrı metalik / pürüzlülük haritası ve yüz seçimi yok
    };
    bool DrawProperties(Material& material, const Options& options = {});

    // Varlık malzemesi düzenlendi: düzenleme bitince (fare bırakılınca) diske yazılır
    void MarkDirty(const std::shared_ptr<Material>& material, const std::string& path);
    void FlushPendingSaves(bool force = false);

    // Mesh Renderer'ın malzeme yuvası + malzeme bölümü (Inspector)
    void DrawMeshRendererMaterials(MeshRendererComponent& component, const std::string& entityName);

    // Content Browser'da seçili .cqmat
    void DrawAssetInspector(const std::string& path);

    // Bir .cqmat dosyasından Mesh Renderer'a ata (sürükle-bırak, otomasyon)
    bool AssignMaterial(MeshRendererComponent& component, const std::string& path);

    // "Open Shader Graph": düzenleyici paneli açar (EditorLayer bağlar)
    void SetOpenGraphCallback(std::function<void(const std::string& materialPath)> callback);
    void OpenGraph(const std::string& materialPath);

    // Genel varlık yuvası (Unity nesne alanı): dosya adı düğmesi, Content Browser'dan sürükle-bırak, seçici, temizle.
    // path proje içi mutlak yol olarak tutulur (sahne kaydında göreli yazılır). Değişince true.
    bool AssetPathSlot(const char* label, std::string& path, const char* extension, const char* typeName, const char* tooltip = nullptr);

    // Yeni malzeme dosyası için benzersiz yol: <Assets>/Materials/<name>.cqmat
    std::string UniqueMaterialPath(const std::string& directory, const std::string& name);
}
