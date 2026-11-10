#pragma once

#include "Core/Base/Base.h"

#include <cstdint>

// World Partition (Unreal): dünya ızgara hücrelerine bölünür; oyunda akış kaynaklarının (Streaming Source, yoksa ana kamera) yükleme
// menzilindeki hücreler yüklü, uzaktakiler boşaltılır (çizilmez, fizik / betik çalışmaz). Büyük açık dünyalar tek sahnede kalır.
namespace Conqueror
{
    class Scene;

    struct WorldPartitionComponent
    {
        bool EnableStreaming = true;
        float CellSize = 64.0f;               // m
        float LoadingRange = 128.0f;          // m (akış kaynağından hücre kenarına)
        float UpdateInterval = 0.25f;         // s
        // Bilgi
        int Cells = 0;
        int LoadedCells = 0;
        int StreamedActors = 0;
    };

    struct WorldPartitionStreamingSourceComponent
    {
        bool Enabled = true;
    };

    // Uzamsal yüklemeye girmeyen, her zaman yüklü nesne (Unreal "Is Spatially Loaded" kapalı)
    struct AlwaysLoadedComponent
    {
        bool Enabled = true;                  // (boş bileşen entt'de etiket olur; alan gerekli)
    };

    namespace WorldPartition
    {
        CQ_API void BeginPlay(Scene& scene);
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void Forget(Scene& scene);
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
    }

    void RegisterWorldPartitionComponents();
}
