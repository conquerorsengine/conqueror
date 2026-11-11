#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>

// Nanite benzeri GPU güdümlü sanal geometri (Virtual Geometry bileşeni): kümelerin seçimi CPU'da değil GPU'da yapılır — ekran uzayı
// hatasına göre LOD (çatlaksız küme DAG kesiti), görüş konisi ve HZB ile örtme. Aynı küme meshini ve malzemeyi kullanan tüm nesneler
// tek dolaylı çoklu çizimde (glMultiDrawElementsIndirectCount): binlerce nesnede CPU maliyeti nesne başına bir matris.
// İki geçişli örtme (Nanite): 1. geçiş önceki karede görünen kümeleri çizer; opak çizim bitince derinlikten HZB (en uzak derinlik
// piramidi) kurulur, 2. geçiş kalan kümelerden artık görünenleri çizer (kamera dönünce açılan yerler aynı karede dolar, gecikme ya
// da delik yok); görünürlük bir sonraki kareye kalır. Gölge kademeleri aynı seçimi kademenin ışık hacmi ve daha kaba eşikle kullanır.
// Desteklenmeyen sürücüde (GL 4.6 / ARB_indirect_parameters yok) eski CPU seçimi kullanılır.
namespace Conqueror
{
    class Scene;
    struct RenderCamera;

    namespace ClusterRenderer
    {
        struct Stats
        {
            int Instances = 0;               // bu karede GPU yolundaki nesne (alt mesh) sayısı
            int Batches = 0;                 // dolaylı çizim partisi
            uint32_t ClustersDrawn = 0;      // (birkaç kare önceki) çizilen küme, 1. + 2. geçiş
            uint32_t TrianglesDrawn = 0;
            uint32_t ClustersOccluded = 0;   // görüş konisinde ve LOD'da seçilmiş ama örtülü (çizilmedi)
        };

        CQ_API bool Supported();
        // Bu karenin Virtual Geometry'li nesneleri (gölgelerden önce): drawable, nesnenin bu görünümde çizilip çizilmediği (LOD grubu,
        // occlusion culling ...). Hazır olmayan (kümeleri arka planda yapılıyor) nesneler eski yoldan çizilir
        // view: aynı karede çizilen görünümler (editör, oyun, yakalamalar) kendi görünürlük bitlerini tutar
        CQ_API void Gather(Scene& scene, const RenderCamera& camera, uint32_t view, const std::function<bool(uint32_t)>& drawable);
        // Nesne bu karede GPU yolunda mı (modeller ve gölge döngüsü onu atlar)
        CQ_API bool Handles(uint32_t entity);
        // Gölge kademesi: ışık hacmi düzlemleri; çizimden önce derinlik shader'ını (örnekli) yeniden bağlar
        CQ_API void DrawShadows(const glm::vec4* planes, const std::function<void()>& bindDepthShader);
        // Ana görünüm, 1. geçiş (önceki karede görünenler)
        CQ_API void DrawOpaque(const RenderCamera& camera);
        // Opak çizim bitti: HZB + 2. geçiş (artık görünenler) ve görünürlük güncellemesi
        CQ_API void DrawDisoccluded(const RenderCamera& camera);
        // Bu görünümün sonu (bir sonraki RenderScene3D yeniden toplar)
        CQ_API void EndView();
        CQ_API const Stats& GetStats();
        // Örtme ile ayıklama (r.Nanite.Occlusion): kapalıyken her kare görüş + LOD'a uyan tüm kümeler çizilir
        CQ_API void SetOcclusionCulling(bool on);
        CQ_API bool GetOcclusionCulling();
        // r.Nanite: kapalıyken eski CPU seçimi (karşılaştırma / sorun giderme)
        CQ_API void SetEnabled(bool on);
        CQ_API bool GetEnabled();
    }
}
