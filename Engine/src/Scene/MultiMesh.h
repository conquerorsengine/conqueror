#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// MultiMesh (Godot MultiMeshInstance3D): aynı meshin binlerce kopyası (çimen, ağaç, kaya, kalabalık) tek çizim
// çağrısında GPU örneklemeyle çizilir. Örnekler alan içinde rastgele dağıtılır (isteğe bağlı terrain'e oturur),
// ızgaraya dizilir ya da betikten tek tek yerleştirilir. Gölgeler de örneklenmiş tek çağrıdır.
namespace Conqueror
{
    class Scene;
    class Mesh;
    class Material;
    class Texture2D;
    struct Model;

    enum class MultiMeshSource : int { Primitive = 0, Model };
    enum class MultiMeshMode : int { Scatter = 0, Grid, Manual };

    struct MultiMeshComponent
    {
        MultiMeshSource Source = MultiMeshSource::Primitive;
        int Primitive = 0;                    // 0 küp, 1 küre, 2 düzlem, 3 silindir
        std::string ModelPath;
        glm::vec4 Color{ 1.0f };
        float Metallic = 0.0f;
        float Roughness = 0.6f;
        std::string Texture;                  // ilkel kaynakta isteğe bağlı albedo dokusu

        MultiMeshMode Mode = MultiMeshMode::Scatter;
        int Count = 200;
        glm::vec3 Area{ 20.0f, 0.0f, 20.0f }; // yerel kutu (merkezli)
        int Seed = 1;
        glm::vec2 ScaleRange{ 0.8f, 1.2f };
        bool RandomYaw = true;
        float ColorVariation = 0.15f;         // örnek başına parlaklık / ton oynaması
        bool SnapToTerrain = false;           // sahnedeki terrain yüzeyine oturt
        glm::vec3 GridCount{ 10.0f, 1.0f, 10.0f };
        glm::vec3 Spacing{ 2.0f };
        std::string InstanceData;             // Manual: "x y z yaw scale r g b a;..." (betik / kayıt)

        bool CastShadows = true;
        bool ReceiveShadows = true;
        float CullDistance = 0.0f;            // kamera bu uzaklığın ötesindeyse küme çizilmez (0 = kapalı)

        // Çalışma zamanı (yerel uzay)
        std::vector<glm::mat4> Transforms;
        std::vector<glm::vec4> Colors;
        uint64_t Signature = 0;               // üretim parametreleri imzası (Scatter / Grid)
        uint64_t Version = 0;                 // örnek verisi sürümü (GPU yüklemesi bununla eşlenir)
        bool ManualDirty = false;             // betik değiştirdi: InstanceData yeniden yazılacak
        glm::vec3 BoundsMin{ 0.0f }, BoundsMax{ 0.0f };
        std::shared_ptr<Material> RuntimeMaterial;
        std::shared_ptr<Texture2D> RuntimeTexture;
        std::string LoadedTexture;
        std::shared_ptr<Model> RuntimeModel;
        std::string LoadedModel;
        int Instances = 0;                    // bilgi
        int DrawCalls = 0;                    // bilgi: son karedeki çizim çağrısı
    };

    namespace MultiMesh
    {
        // Örnekleri gerekirse yeniden üret, GPU tamponlarını güncelle (düzenleyicide ve oyunda)
        CQ_API void Update(Scene& scene);
        // Görünür kümeleri çiz (Scene::RenderScene3D)
        CQ_API void Render(Scene& scene);
        // Gölge geçişi: düzlemlerin (kademenin ışık hacmi; nullptr = hepsi) içindeki her örnek aralığı için
        // (mesh, dünya matrisi, örnek tamponu, ilk örnek, örnek sayısı, çift yüz)
        CQ_API void ForEachShadowBatch(Scene& scene, const glm::vec4* planes,
                                       const std::function<void(const Mesh&, const glm::mat4&, uint32_t, uint32_t, uint32_t, bool)>& fn);

        // Betik / düzenleyici yardımcıları (Manual moda geçirir)
        CQ_API int AddInstance(MultiMeshComponent& c, const glm::mat4& local, const glm::vec4& color = glm::vec4(1.0f));
        CQ_API bool SetInstance(MultiMeshComponent& c, int index, const glm::mat4& local);
        CQ_API bool SetInstanceColor(MultiMeshComponent& c, int index, const glm::vec4& color);
        CQ_API void SetCount(MultiMeshComponent& c, int count);
        CQ_API void Clear(MultiMeshComponent& c);
        CQ_API void MarkChanged(MultiMeshComponent& c);

        CQ_API void ForgetScene(Scene* scene);
        CQ_API void ClearCache();               // GPU tamponları (Renderer kapanmadan önce)
    }

    void RegisterMultiMeshComponents();
}
