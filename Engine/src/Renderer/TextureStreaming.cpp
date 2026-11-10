#include "TextureStreaming.h"

#include "Renderer/Renderer.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLTexture.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Time/TimeManager.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Conqueror
{
    namespace
    {
        using Pixels = OpenGLTexture2D::StreamingPixels;

        struct Entry
        {
            OpenGLTexture2D* Tex = nullptr;
            uint64_t Serial = 0;        // kayıt sırası: yeniden yüklenen dokuya eski çözme sonucu yazılmaz
            float CycleMin = 1e9f;      // bu turda istenen en ayrıntılı seviye
            bool Requested = false;
            int Desired = 0;
            bool Pending = false;
        };

        struct Job
        {
            Texture2D* Key = nullptr;
            uint64_t Serial = 0;
            std::future<std::pair<bool, Pixels>> Result;
        };

        struct State
        {
            std::unordered_map<Texture2D*, Entry> Entries;
            std::vector<Job> Jobs;
            uint64_t NextSerial = 1;
            size_t Cursor = 0;          // Renderers Per Frame: sıradaki Mesh Renderer
            bool CycleDone = false;     // bu karede tüm renderer'lar en az bir kez incelendi
        };

        // Bilinçli olarak yok edilmez: çıkışta süren çözme işleri yalnız kendi kopyalarıyla çalışır
        State& S()
        {
            static State& s = *new State();
            return s;
        }

        uint32_t MipCount(uint32_t w, uint32_t h)
        {
            uint32_t levels = 1, m = std::max(w, h);
            while (m > 1) { m >>= 1; ++levels; }
            return levels;
        }

        // Ağın UV yoğunluğu (Unity "mesh UV distribution metric"): UV biriminin nesne uzayında kaç metreye karşılık geldiği.
        // Üçgen alanlarının oranından, ağ başına bir kez
        float MetersPerUV(const Mesh* mesh)
        {
            if (!mesh)
                return 2.0f;
            struct Cached { size_t Vertices = 0; float Value = 2.0f; };
            static std::unordered_map<const Mesh*, Cached>& s_Cache = *new std::unordered_map<const Mesh*, Cached>();
            const auto& v = mesh->GetVertices();
            const auto& idx = mesh->GetIndices();
            auto it = s_Cache.find(mesh);
            if (it != s_Cache.end() && it->second.Vertices == v.size())
                return it->second.Value;
            double areaPos = 0.0, areaUV = 0.0;
            for (size_t i = 0; i + 2 < idx.size(); i += 3)
            {
                if (idx[i] >= v.size() || idx[i + 1] >= v.size() || idx[i + 2] >= v.size())
                    continue;
                const auto& a = v[idx[i]];
                const auto& b = v[idx[i + 1]];
                const auto& c = v[idx[i + 2]];
                areaPos += 0.5 * (double)glm::length(glm::cross(b.Position - a.Position, c.Position - a.Position));
                const glm::vec2 e1 = b.TexCoords - a.TexCoords, e2 = c.TexCoords - a.TexCoords;
                areaUV += 0.5 * std::fabs((double)e1.x * e2.y - (double)e1.y * e2.x);
            }
            float value = areaUV > 1e-10 ? (float)std::sqrt(areaPos / areaUV) : std::max(mesh->GetBoundsRadius() * 2.0f, 0.01f);
            s_Cache[mesh] = { v.size(), value };
            return value;
        }

        const Mesh* PrimitiveMesh(MeshType type)
        {
            switch (type)
            {
                case MeshType::Sphere: return Renderer3D::GetSphereMesh().get();
                case MeshType::Plane: return Renderer3D::GetPlaneMesh().get();
                case MeshType::Cylinder: return Renderer3D::GetCylinderMesh().get();
                default: return Renderer3D::GetCubeMesh().get();
            }
        }

        int MaxLevelFor(const Entry& e)
        {
            return std::clamp(TextureStreaming::Settings().MaxLevelReduction, 0, std::max(0, e.Tex->GetStreamingMaxLevel()));
        }

        // Bir seviyedeki bellek: şu anki bellekten her seviye farkı için 4 kat
        double BytesAt(const Entry& e, int level)
        {
            const double now = (double)e.Tex->GetMemoryBytes();
            return now * std::pow(4.0, (double)(e.Tex->GetStreamingLevel() - level));
        }
    }

    namespace TextureStreaming
    {
        const TextureStreamingSettings& Settings()
        {
            return ProjectSettings::Quality().Streaming;
        }

        int InitialLevel(uint32_t width, uint32_t height)
        {
            const auto& s = Settings();
            if (!s.Enabled)
                return 0;
            return std::clamp(s.MaxLevelReduction, 0, (int)MipCount(width, height) - 1);
        }

        void Register(Texture2D* texture)
        {
            auto* gl = dynamic_cast<OpenGLTexture2D*>(texture);
            if (!gl)
                return;
            Entry& e = S().Entries[texture];
            e.Tex = gl;
            e.Serial = S().NextSerial++;
            e.Pending = false;
            e.Desired = gl->GetStreamingLevel();
            e.CycleMin = 1e9f;
            e.Requested = false;
        }

        void Unregister(Texture2D* texture)
        {
            S().Entries.erase(texture);
        }

        void RequestView(Scene& scene, const RenderCamera& camera, float mipBias)
        {
            State& st = S();
            const auto& set = Settings();
            if (!set.Enabled || st.Entries.empty())
                return;
            auto view = scene.m_Registry.view<TransformComponent, MeshRendererComponent>();
            std::vector<entt::entity> list(view.begin(), view.end());
            if (list.empty())
            {
                st.CycleDone = true;
                return;
            }
            // Ekranda metre başına piksel: izdüşümün dikey ölçeğinden (perspektifte uzaklıkla azalır)
            const float projY = std::fabs(camera.Projection[1][1]);
            const float height = (float)std::max(camera.ViewportHeight, 1u);
            const size_t n = list.size();
            const size_t count = std::min(n, (size_t)std::max(1, set.RenderersPerFrame));
            const size_t start = st.Cursor % n;

            auto requestMesh = [&](const glm::mat4& world, float scale, const Mesh* mesh, const Material* mat) {
                if (!mat)
                    return;
                const std::shared_ptr<Texture2D>* maps[] = { &mat->AlbedoMap, &mat->NormalMap, &mat->MetallicMap, &mat->RoughnessMap, &mat->AOMap, &mat->EmissionMap };
                const glm::vec3 center = mesh ? mesh->GetBoundsCenter() : glm::vec3(0.0f);
                const float radius = (mesh ? mesh->GetBoundsRadius() : 1.0f) * scale;
                const glm::vec3 worldCenter = glm::vec3(world * glm::vec4(center, 1.0f));
                const float dist = std::max(glm::length(camera.Position - worldCenter) - radius, 0.05f);
                const float pixelsPerMeter = camera.Orthographic ? height * projY * 0.5f : height * projY / (2.0f * dist);
                const float metersPerUV = std::max(MetersPerUV(mesh) * scale, 1e-4f);
                const float tiling = std::max(std::max(std::fabs(mat->Tiling.x), std::fabs(mat->Tiling.y)), 1e-3f);
                for (const auto* map : maps)
                {
                    if (!*map)
                        continue;
                    auto it = st.Entries.find(map->get());
                    if (it == st.Entries.end())
                        continue;
                    const float texels = (float)std::max((*map)->GetWidth(), (*map)->GetHeight());
                    // Dünya hizalı malzemede Tiling = metre başına tekrar
                    const float texelsPerMeter = mat->WorldAligned ? texels * tiling : texels * tiling / metersPerUV;
                    const float level = std::log2(std::max(texelsPerMeter / std::max(pixelsPerMeter, 1e-4f), 1.0f)) + mipBias;
                    it->second.CycleMin = std::min(it->second.CycleMin, level);
                    it->second.Requested = true;
                }
            };

            for (size_t i = 0; i < count; ++i)
            {
                const entt::entity h = list[(start + i) % n];
                if (!scene.IsEntityActive(h))
                    continue;
                const auto& tc = view.get<TransformComponent>(h);
                const auto& mr = view.get<MeshRendererComponent>(h);
                const glm::mat4& world = tc.WorldTransform;
                const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
                if (mr.IsModel())
                {
                    if (!mr.ModelData)
                        continue;
                    const auto& model = *mr.ModelData;
                    for (size_t m = 0; m < model.Meshes.size(); ++m)
                        requestMesh(world, scale, model.Meshes[m].get(), m < model.Materials.size() ? model.Materials[m].get() : nullptr);
                }
                else
                {
                    const Mesh* mesh = mr.CustomMesh ? mr.CustomMesh.get() : PrimitiveMesh(mr.Type);
                    requestMesh(world, scale, mesh, mr.MaterialInstance.get());
                }
            }
            st.Cursor = (start + count) % n;
            if (start + count >= n)
                st.CycleDone = true;
        }

        void Tick()
        {
            State& st = S();
            const auto& set = Settings();
            if (st.Entries.empty() && st.Jobs.empty())
                return;

            // 1) Biten çözmeler: doku hâlâ aynıysa (silinmemiş, yeniden yüklenmemiş) yüklenir
            for (auto it = st.Jobs.begin(); it != st.Jobs.end();)
            {
                if (it->Result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                {
                    ++it;
                    continue;
                }
                auto result = it->Result.get();
                auto e = st.Entries.find(it->Key);
                if (e != st.Entries.end() && e->second.Serial == it->Serial)
                {
                    e->second.Pending = false;
                    if (result.first && result.second.Level < e->second.Tex->GetStreamingLevel())
                        e->second.Tex->StreamUpload(result.second);
                }
                it = st.Jobs.erase(it);
            }

            // 2) İstenen seviyeler: akış kapalıysa tam boyut; tur tamamlandıysa turda istenen (istenmeyen: en kaba izinli)
            for (auto& [key, e] : st.Entries)
            {
                if (!e.Tex->IsStreamed())
                    continue;
                if (!set.Enabled)
                    e.Desired = 0;
                else if (st.CycleDone)
                {
                    const int maxLevel = MaxLevelFor(e);
                    e.Desired = e.Requested ? std::clamp((int)std::floor(e.CycleMin), 0, maxLevel) : maxLevel;
                    e.CycleMin = 1e9f;
                    e.Requested = false;
                }
            }
            st.CycleDone = false;

            // 3) Bellek bütçesi: aşılıyorsa önce düşük öncelikli, sonra büyük dokular birer seviye küçülür
            if (set.Enabled)
            {
                double resident = 0.0, desired = 0.0;
                std::vector<Entry*> order;
                for (auto& [key, e] : st.Entries)
                    if (e.Tex->IsStreamed())
                    {
                        resident += (double)e.Tex->GetMemoryBytes();
                        desired += BytesAt(e, e.Desired);
                        order.push_back(&e);
                    }
                const double total = (double)Renderer::GetStats().TextureMemory * 1024.0 * 1024.0;
                const double other = std::max(0.0, total - resident);
                const double budget = (double)set.MemoryBudgetMB * 1024.0 * 1024.0;
                if (other + desired > budget && !order.empty())
                {
                    std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) {
                        const int pa = a->Tex->GetImportSettings().StreamingPriority, pb = b->Tex->GetImportSettings().StreamingPriority;
                        if (pa != pb) return pa < pb;
                        return a->Tex->GetMemoryBytes() > b->Tex->GetMemoryBytes();
                    });
                    bool changed = true;
                    while (other + desired > budget && changed)
                    {
                        changed = false;
                        for (Entry* e : order)
                        {
                            if (other + desired <= budget)
                                break;
                            if (e->Desired < MaxLevelFor(*e))
                            {
                                desired -= BytesAt(*e, e->Desired) - BytesAt(*e, e->Desired + 1);
                                ++e->Desired;
                                changed = true;
                            }
                        }
                    }
                }
            }

            // 4) Uygula: kaba seviyeye iniş GPU'da anında; ayrıntılı seviye arka planda çözülür (aynı anda en çok 4 iş)
            const int maxJobs = std::clamp(set.MaxIORequests, 1, 4);
            for (auto& [key, e] : st.Entries)
            {
                if (!e.Tex->IsStreamed())
                    continue;
                const int current = e.Tex->GetStreamingLevel();
                if (e.Desired > current)
                    e.Tex->StreamDropTo(e.Desired);
                else if (e.Desired < current && !e.Pending && (int)st.Jobs.size() < maxJobs)
                {
                    Job job;
                    job.Key = key;
                    job.Serial = e.Serial;
                    const std::string path = e.Tex->GetPath();
                    const TextureImportSettings settings = e.Tex->GetImportSettings();
                    const int level = e.Desired;
                    job.Result = std::async(std::launch::async, [path, settings, level]() {
                        std::pair<bool, Pixels> r;
                        r.first = OpenGLTexture2D::DecodeStreamingLevel(path, settings, level, r.second);
                        return r;
                    });
                    e.Pending = true;
                    st.Jobs.push_back(std::move(job));
                }
            }
        }

        void RequestPreloads(Scene& scene, uint32_t width, uint32_t height)
        {
            // StreamingSetPreloading: süre bitene kadar bu kameranın dokuları da istenir (kamera henüz çizilmese de)
            auto view = scene.m_Registry.view<TransformComponent, CameraComponent, StreamingControllerComponent>();
            const float dt = TimeManager::GetUnscaledDeltaTime();
            for (auto h : view)
            {
                auto& controller = view.get<StreamingControllerComponent>(h);
                if (controller.PreloadTimeLeft <= 0.0f)
                    continue;
                controller.PreloadTimeLeft = std::max(0.0f, controller.PreloadTimeLeft - dt);
                const RenderCamera rc = RenderCamera::FromSceneCamera(view.get<CameraComponent>(h).Camera, view.get<TransformComponent>(h).WorldTransform,
                                                                      width, height);
                RequestView(scene, rc, controller.StreamingMipBias);
            }
        }

        Stats GetStats()
        {
            Stats s;
            const auto& set = Settings();
            s.Enabled = set.Enabled;
            s.BudgetMB = set.MemoryBudgetMB;
            s.TotalMB = Renderer::GetStats().TextureMemory;
            for (const auto& [key, e] : S().Entries)
            {
                if (!e.Tex->IsStreamed())
                    continue;
                ++s.StreamingTextures;
                s.StreamingMB += (float)((double)e.Tex->GetMemoryBytes() / (1024.0 * 1024.0));
                s.DesiredMB += (float)(BytesAt(e, e.Desired) / (1024.0 * 1024.0));
            }
            s.PendingLoads = (int)S().Jobs.size();
            return s;
        }

        int PendingCount()
        {
            int pending = (int)S().Jobs.size();
            for (const auto& [key, e] : S().Entries)
                if (e.Tex->IsStreamed() && e.Desired < e.Tex->GetStreamingLevel())
                    ++pending;
            return pending;
        }
    }

    template<> void Scene::OnComponentAdded<StreamingControllerComponent>(Entity, StreamingControllerComponent&) {}

    void RegisterTextureStreamingComponents()
    {
        using C = StreamingControllerComponent;
        ComponentDef<C>("Streaming Controller", "StreamingControllerComponent", "Rendering", ICON_FA_DOWNLOAD)
            .Describe("Texture Streaming (Project Settings > Quality): textures with Streaming Mipmaps load the mip levels this camera needs.\n"
                      "With Add All Cameras off, only cameras with a Streaming Controller (and the Scene view) request textures.")
            .Requires("CameraComponent")
            .Field("StreamingMipBias", "Mip Map Bias", &C::StreamingMipBias).Range(-3.0f, 3.0f, 0.05f)
                .Tooltip("Positive: coarser mip levels (less memory). Negative: more detailed.")
            .Field("PreloadTimeLeft", "Preloading", &C::PreloadTimeLeft).ReadOnly().Format("%.2f s")
                .Tooltip("StreamingSetPreloading(camera, seconds): requests this camera's textures before it renders.");
    }
}
