#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>

// Ray Traced GI (Unreal Lumen benzeri dinamik küresel aydınlatma, DDGI yöntemi): kameranın çevresinde dünya ızgarasına oturan
// ışık probları her karede (bir bölümü, ışın bütçesiyle) sahnenin iki seviyeli BVH'sine (RayTracingScene) ışın izler. Işının
// çarptığı yüzeyin doğrudan ışığı (güneş, nokta, spot, alan; gölge ışınlı), ışıması ve önceki karenin problarından gelen dolaylı
// ışığı (sonsuz sekme), ıskalayan ışınların gökyüzü ışığı problara toplanır. Problar ışınımı ve yüzey uzaklığını (görünürlük:
// duvar arkasından ışık sızmaz) oktahedral atlaslarda tutar; zamanla harmanlanır, kapalı yerlerdeki problar dışarı kaydırılır,
// geometrinin içinde kalanlar kapatılır. Forward ışıklandırma (Lighting.glsl) ortam ışığı yerine probları örnekler: kapalı mekân
// kararır, güneşin vurduğu yüzeyler çevresini aydınlatır ve rengini taşır, ışıyan yüzeyler ışık verir. Pişirme yok: ışıklar,
// nesneler ve gökyüzü değişince birkaç karede güncellenir. Hacim kamerayla birlikte hücre hücre kayar (sınırsız dünya).
namespace Conqueror
{
    class Scene;
    struct RenderCamera;
    struct PostProcessSettings;

    namespace RayTracedGI
    {
        struct Stats
        {
            bool Active = false;
            glm::ivec3 Counts{ 0 };
            int Probes = 0;
            int ProbesUpdated = 0;           // bu karede
            int RaysTraced = 0;              // bu karede
            int InactiveProbes = 0;          // son ölçüm (geometrinin içinde)
            float Spacing = 0.0f;
            int Triangles = 0;
            int Instances = 0;
            int PendingBuilds = 0;
        };

        // Görünümün opak çiziminden önce (ışıklar Renderer3D'ye verildikten sonra): problar güncellenir ve Renderer3D'ye bağlanır.
        // Kapalıysa (ya da ortografik kamera) bağlantı kaldırılır. view: aynı karede çizilen görünümler (editör / oyun) ayrı hacim.
        // trace false: ışın izlenmez, görünümün mevcut hacmi bağlanır (ikincil kameralar)
        CQ_API void Update(Scene& scene, const RenderCamera& camera, const PostProcessSettings& settings, uint32_t view, bool trace = true);
        // Görünüm bitti: başka çizimler (önizleme, küçük resim) problardan etkilenmesin
        CQ_API void EndView();
        // Görünümün hacmi bir sonraki karede baştan kurulur (ışınlanma, sahne değişimi)
        CQ_API void Reset();
        CQ_API const Stats& GetStats();
        // Hata ayıklama: bir noktanın problardan aldığı ışınım (CPU tarafında hesaplanmaz; test / gösterim için son prob verisi)
        CQ_API bool ReadProbe(uint32_t view, const glm::ivec3& cell, glm::vec3& irradianceUp, int& state);
    }
}
