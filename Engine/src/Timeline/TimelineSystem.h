#pragma once

#include "Core/Base/Base.h"
#include "Timeline/Timeline.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;
    class Entity;

    // Unity: PlayableDirector — bir zaman çizelgesini oynatır ve parçalarını sahne varlıklarına bağlar
    struct PlayableDirectorComponent
    {
        std::string Timeline;            // .cqtimeline (proje göreli)
        bool PlayOnAwake = true;
        int WrapMode = 0;                // 0 Hold (son karede kalır), 1 Loop, 2 None (durur ve başa döner)
        float Speed = 1.0f;
        std::vector<uint64_t> Bindings;  // parça başına varlık UUID (0 = bu varlık)

        // Çalışma zamanı (serileştirilmez)
        double Time = 0.0;
        bool Playing = false;
        bool Started = false;
        double LastSignalTime = -1.0;
    };

    class CQ_API TimelineSystem
    {
    public:
        // Oyun: PlayOnAwake, zaman ilerletme, değerlendirme, sinyaller, ses
        static void Update(Scene& scene, float dt);
        // Editör önizlemesi / betik: belirli bir zamanda değerlendir (sinyal/ses yok)
        static void Evaluate(Scene& scene, Entity director, double time);
        // Verilen (düzenlenmekte olan, kaydedilmemiş olabilir) varlıkla değerlendir
        static void Evaluate(Scene& scene, Entity director, const TimelineAsset& asset, double time);

        static void Play(Scene& scene, Entity director);
        static void Pause(Scene& scene, Entity director);
        static void Stop(Scene& scene, Entity director);
        static void SetTime(Scene& scene, Entity director, double time);

        // Önbellekli varlık (dosya değişirse yeniden yüklenir)
        static std::shared_ptr<TimelineAsset> GetAsset(const std::string& path);
        static void InvalidateAsset(const std::string& path);
        static std::filesystem::path ResolvePath(const std::string& path);
        static double GetDuration(const PlayableDirectorComponent& director);

        // Parçanın bağlı olduğu varlık (bağlantı yoksa yönetmen varlığı)
        static Entity ResolveBinding(Scene& scene, Entity director, size_t trackIndex);

        static void ForgetScene(Scene* scene);
        static void Shutdown();
    };
}
