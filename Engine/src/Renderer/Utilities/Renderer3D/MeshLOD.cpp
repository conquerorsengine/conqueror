#include "MeshLOD.h"
#include "MeshSimplifier.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <mutex>
#include <vector>

namespace Conqueror::MeshLOD
{
    namespace
    {
        struct LevelData
        {
            std::vector<Vertex> Vertices;
            std::vector<uint32_t> Indices;
        };

        struct Job
        {
            std::weak_ptr<Mesh> Target;
            const Mesh* Key = nullptr;
            Settings Config;
            std::future<std::vector<LevelData>> Result;
        };

        std::vector<Job>& Jobs()
        {
            static std::vector<Job> jobs;
            return jobs;
        }

        // Seviyeler zincirleme: her biri bir öncekinden (hızlı ve tutarlı)
        std::vector<LevelData> BuildLevels(std::vector<Vertex> vertices, std::vector<uint32_t> indices, Settings settings)
        {
            std::vector<LevelData> levels;
            const int count = std::clamp(settings.Levels, 1, 4);
            const float reduction = std::clamp(settings.Reduction, 0.05f, 0.95f);
            size_t triangles = indices.size() / 3;
            for (int i = 0; i < count; ++i)
            {
                MeshSimplifier::Options o;
                o.TargetTriangles = (uint32_t)std::max<size_t>(4, (size_t)((float)triangles * reduction));
                MeshSimplifier::Result r = MeshSimplifier::Simplify(vertices, indices, o);
                if (r.Indices.empty() || r.Indices.size() / 3 >= triangles)
                    break;   // daha fazla sadeleşmiyor
                triangles = r.Indices.size() / 3;
                vertices = r.Vertices;
                indices = r.Indices;
                levels.push_back({ std::move(r.Vertices), std::move(r.Indices) });
                if (triangles <= 8)
                    break;
            }
            return levels;
        }

        void Attach(Mesh& mesh, std::vector<LevelData>& levels, const Settings& settings)
        {
            std::vector<Mesh::LODLevel> lods;
            for (size_t i = 0; i < levels.size(); ++i)
            {
                Mesh::LODLevel l;
                l.LODMesh = std::make_shared<Mesh>(levels[i].Vertices, levels[i].Indices);
                l.ScreenSize = settings.ScreenSizes[std::min<size_t>(i, 3)];
                lods.push_back(std::move(l));
            }
            mesh.SetLODs(std::move(lods), Signature(settings));
        }
    }

    uint64_t Signature(const Settings& s)
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix((uint64_t)std::clamp(s.Levels, 1, 4));
        mix((uint64_t)std::llround(s.Reduction * 10000.0f));
        for (float f : s.ScreenSizes)
            mix((uint64_t)std::llround(f * 100000.0f));
        return h | 1ull;   // 0 = LOD yok
    }

    int Generate(const std::shared_ptr<Mesh>& mesh, const Settings& settings)
    {
        if (!mesh || mesh->GetIndexCount() < 24)
            return 0;
        auto levels = BuildLevels(mesh->GetVertices(), mesh->GetIndices(), settings);
        Attach(*mesh, levels, settings);
        return (int)levels.size();
    }

    bool RequestAsync(const std::shared_ptr<Mesh>& mesh, const Settings& settings)
    {
        if (!mesh || mesh->GetIndexCount() < 24)
            return false;
        if (mesh->GetLODSignature() == Signature(settings))
            return true;
        for (auto& job : Jobs())
            if (job.Key == mesh.get())
                return true;
        // Küçük meshler hemen (iş parçacığı maliyetine değmez)
        if (mesh->GetIndexCount() / 3 < 4000)
        {
            Generate(mesh, settings);
            return true;
        }
        Job job;
        job.Target = mesh;
        job.Key = mesh.get();
        job.Config = settings;
        job.Result = std::async(std::launch::async, BuildLevels, mesh->GetVertices(), mesh->GetIndices(), settings);
        Jobs().push_back(std::move(job));
        return true;
    }

    bool IsPending(const Mesh* mesh)
    {
        for (auto& job : Jobs())
            if (job.Key == mesh)
                return true;
        return false;
    }

    void Update()
    {
        auto& jobs = Jobs();
        for (size_t i = 0; i < jobs.size();)
        {
            Job& job = jobs[i];
            if (job.Result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++i;
                continue;
            }
            auto levels = job.Result.get();
            if (auto mesh = job.Target.lock())
            {
                Attach(*mesh, levels, job.Config);
                CQ_CORE_INFO("[AutoLOD] {0} LOD levels generated ({1} -> {2} triangles)", levels.size(), mesh->GetIndexCount() / 3,
                             levels.empty() ? mesh->GetIndexCount() / 3 : (uint32_t)(levels.back().Indices.size() / 3));
            }
            jobs.erase(jobs.begin() + (long)i);
        }
    }

    void Clear(const std::shared_ptr<Mesh>& mesh)
    {
        if (mesh)
            mesh->SetLODs({}, 0);
    }
}
