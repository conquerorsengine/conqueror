#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <memory>

// Virtual Geometry (Unreal Nanite benzeri): modelin statik meshleri küme DAG'ına dönüştürülür (arka planda), sonra
// her karede ekran uzayı hatasına göre küme seçilerek çizilir — yakında tam ayrıntı, uzakta birkaç küme, çatlaksız.
// Kümeler mesh varlığına bağlıdır (aynı modeli kullanan tüm nesneler paylaşır). Gölge geçişi daha kaba eşik kullanır.
namespace Conqueror
{
    class Scene;
    class Mesh;
    class ClusterMesh;

    struct VirtualGeometryComponent
    {
        bool Enabled = true;
        float ErrorThreshold = 1.0f;      // piksel: küme sadeleşme hatası ekranda bundan büyükse ayrıntılı kümeler
        float ShadowErrorThreshold = 4.0f;
        int ClusterSize = 128;            // küme başına üçgen
        bool VisualizeClusters = false;   // her küme ayrı renkte

        // Bilgi (çalışma zamanı)
        bool Ready = false;
        int Clusters = 0;
        int Levels = 0;
        int TrianglesTotal = 0;
        int TrianglesDrawn = 0;
        int ClustersDrawn = 0;
    };

    namespace VirtualGeometry
    {
        // Hazırsa küme meshi (yoksa arka planda yapımı başlatır ve nullptr döner)
        CQ_API std::shared_ptr<ClusterMesh> Acquire(const std::shared_ptr<Mesh>& mesh, int clusterSize, bool wait = false);
        // Biten yapımları GPU'ya yükle (her karede ana iş parçacığında)
        CQ_API void Update();
        CQ_API void ClearCache();
    }

    void RegisterVirtualGeometryComponents();
}
