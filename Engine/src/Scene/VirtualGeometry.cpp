#include "VirtualGeometry.h"
#include "Scene/ComponentRegistry.h"
#include "Renderer/Utilities/Renderer3D/ClusterMesh.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Core/Logging/Log.h"

#include <IconsFontAwesome6.h>

#include <chrono>
#include <future>
#include <map>
#include <utility>

namespace Conqueror
{
    namespace
    {
        struct Entry
        {
            std::weak_ptr<Mesh> Source;
            std::shared_ptr<ClusterMesh> Result;
            std::future<std::shared_ptr<ClusterMesh>> Job;
            bool Pending = false;
        };

        std::map<std::pair<const Mesh*, int>, Entry>& Cache()
        {
            static std::map<std::pair<const Mesh*, int>, Entry> cache;
            return cache;
        }

        void Finish(Entry& e)
        {
            e.Result = e.Job.get();
            e.Pending = false;
            if (e.Result)
            {
                e.Result->Upload();
                CQ_CORE_INFO("[VirtualGeometry] {0} clusters, {1} levels ({2} triangles)", e.Result->GetClusters().size(), e.Result->GetLevelCount(),
                             e.Result->GetLeafTriangleCount());
            }
        }
    }

    namespace VirtualGeometry
    {
        std::shared_ptr<ClusterMesh> Acquire(const std::shared_ptr<Mesh>& mesh, int clusterSize, bool wait)
        {
            if (!mesh || mesh->GetIndexCount() < 3)
                return nullptr;
            auto& e = Cache()[{ mesh.get(), clusterSize }];
            if (e.Result && !e.Source.expired())
                return e.Result;
            if (!e.Pending)
            {
                e.Source = mesh;
                e.Result.reset();
                e.Pending = true;
                e.Job = std::async(std::launch::async, [verts = mesh->GetVertices(), idx = mesh->GetIndices(), clusterSize]() {
                    return ClusterMesh::Build(verts, idx, (uint32_t)clusterSize);
                });
            }
            if (wait)
            {
                e.Job.wait();
                Finish(e);
                return e.Result;
            }
            return nullptr;
        }

        void Update()
        {
            for (auto& [key, e] : Cache())
                if (e.Pending && e.Job.valid() && e.Job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    Finish(e);
        }

        void ClearCache()
        {
            for (auto& [key, e] : Cache())
                if (e.Pending && e.Job.valid())
                    e.Job.wait();
            Cache().clear();
        }
    }

    void RegisterVirtualGeometryComponents()
    {
        using C = VirtualGeometryComponent;
        ComponentDef<C>("Virtual Geometry", "VirtualGeometryComponent", "Rendering", ICON_FA_CUBES)
            .Describe("Nanite-style virtualized geometry for this model: meshes become a cluster hierarchy (built in the background) and every frame "
                      "only the clusters needed for ~1 pixel of error are drawn, so very dense meshes stay cheap at any distance.")
            .Field("Enabled", "Enabled", &C::Enabled)
            .Field("ErrorThreshold", "Error Threshold (px)", &C::ErrorThreshold).Range(0.1f, 32.0f, 0.05f)
            .Field("ShadowErrorThreshold", "Shadow Error (px)", &C::ShadowErrorThreshold).Range(0.1f, 64.0f, 0.1f)
            .Field("ClusterSize", "Cluster Size", &C::ClusterSize).Range(32.0f, 256.0f, 1.0f)
            .Field("VisualizeClusters", "Visualize Clusters", &C::VisualizeClusters)
            .Field("Ready", "Ready", &C::Ready).ReadOnly()
            .Field("Clusters", "Clusters", &C::Clusters).ReadOnly()
            .Field("Levels", "Levels", &C::Levels).ReadOnly()
            .Field("TrianglesTotal", "Triangles (full)", &C::TrianglesTotal).ReadOnly()
            .Field("TrianglesDrawn", "Triangles Drawn", &C::TrianglesDrawn).ReadOnly()
            .Field("ClustersDrawn", "Clusters Drawn", &C::ClustersDrawn).ReadOnly();
    }
}
