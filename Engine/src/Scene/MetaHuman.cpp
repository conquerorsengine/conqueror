#include "MetaHuman.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/BlendShapes.h"
#include "Core/Animation/Animator.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        bool Has(const std::string& s, const char* word) { return s.find(word) != std::string::npos; }

        // Ad eşleme biçimi: küçük harf, yalnız harf / rakam, "Maya.ön ek" atılır, sondaki left/right -> l/r
        std::string NormalizeShape(const std::string& name)
        {
            std::string n = name;
            if (const size_t dot = n.find_last_of('.'); dot != std::string::npos)
                n = n.substr(dot + 1);
            std::string out;
            for (unsigned char c : n)
                if (std::isalnum(c)) out += (char)std::tolower(c);
            auto endsWith = [&](const char* suf) {
                const size_t k = std::strlen(suf);
                return out.size() > k && out.compare(out.size() - k, k, suf) == 0;
            };
            if (endsWith("left")) out = out.substr(0, out.size() - 4) + "l";
            else if (endsWith("right")) out = out.substr(0, out.size() - 5) + "r";
            if (out.rfind("arkit", 0) == 0) out = out.substr(5);
            return out;
        }

        // ── Live Link Face kayıtları (önbellek) ──
        struct FaceRecording
        {
            std::vector<std::string> Curves;
            std::vector<float> Times;                 // saniye (ilk satır 0)
            std::vector<std::vector<float>> Rows;     // satır başına eğri değerleri
            float Duration = 0.0f;
            fs::file_time_type Stamp{};
        };

        float ParseTimecode(const std::string& tc, float fps)
        {
            // "hh:mm:ss:ff.fff" (Live Link Face) ya da saniye
            std::vector<double> parts;
            std::stringstream ss(tc);
            std::string p;
            while (std::getline(ss, p, ':'))
                parts.push_back(std::atof(p.c_str()));
            if (parts.size() == 4)
                return (float)(parts[0] * 3600.0 + parts[1] * 60.0 + parts[2] + parts[3] / std::max(fps, 1.0f));
            if (parts.size() == 3)
                return (float)(parts[0] * 3600.0 + parts[1] * 60.0 + parts[2]);
            return parts.empty() ? 0.0f : (float)parts.back();
        }

        const FaceRecording* LoadRecording(const std::string& path, float fps)
        {
            static auto& cache = *new std::unordered_map<std::string, FaceRecording>();
            std::error_code ec;
            const auto stamp = fs::last_write_time(path, ec);
            if (ec)
                return nullptr;
            auto it = cache.find(path);
            if (it != cache.end() && it->second.Stamp == stamp)
                return &it->second;
            std::ifstream f(path);
            if (!f)
                return nullptr;
            FaceRecording rec;
            rec.Stamp = stamp;
            std::string line;
            if (!std::getline(f, line))
                return nullptr;
            std::vector<std::string> header;
            {
                std::stringstream ss(line);
                std::string cell;
                while (std::getline(ss, cell, ','))
                {
                    while (!cell.empty() && (cell.back() == '\r' || cell.back() == ' ')) cell.pop_back();
                    header.push_back(cell);
                }
            }
            int timeCol = -1;
            std::vector<int> cols;
            for (int i = 0; i < (int)header.size(); ++i)
            {
                const std::string h = Lower(header[(size_t)i]);
                if (h == "timecode" || h == "time") timeCol = i;
                else if (h != "blendshapecount" && !h.empty())
                {
                    cols.push_back(i);
                    rec.Curves.push_back(header[(size_t)i]);
                }
            }
            float first = -1.0f;
            int rowIndex = 0;
            while (std::getline(f, line))
            {
                if (line.empty() || line == "\r")
                    continue;
                std::vector<std::string> cells;
                std::stringstream ss(line);
                std::string cell;
                while (std::getline(ss, cell, ','))
                    cells.push_back(cell);
                float t = timeCol >= 0 && timeCol < (int)cells.size() ? ParseTimecode(cells[(size_t)timeCol], fps) : (float)rowIndex / std::max(fps, 1.0f);
                if (first < 0.0f) first = t;
                t -= first;
                if (!rec.Times.empty() && t < rec.Times.back())
                    t = rec.Times.back();   // gün dönümü / bozuk zaman kodu
                std::vector<float> values(cols.size(), 0.0f);
                for (size_t c = 0; c < cols.size(); ++c)
                    if (cols[c] < (int)cells.size())
                        values[c] = (float)std::atof(cells[(size_t)cols[c]].c_str());
                rec.Times.push_back(t);
                rec.Rows.push_back(std::move(values));
                ++rowIndex;
            }
            if (rec.Rows.empty())
                return nullptr;
            rec.Duration = rec.Times.back();
            cache[path] = std::move(rec);
            return &cache[path];
        }

        const Pose* FinalPoseOf(Scene& scene, entt::entity e)
        {
            auto& reg = scene.m_Registry;
            if (const auto* ac = reg.try_get<AnimationComponent>(e); ac && !ac->FinalPose.empty())
                return &ac->FinalPose;
            if (const auto* an = reg.try_get<AnimatorComponent>(e); an && !an->FinalPose.empty())
                return &an->FinalPose;
            return nullptr;
        }

        const Model* SkinnedModel(Scene& scene, entt::entity e)
        {
            const auto* mr = scene.m_Registry.try_get<MeshRendererComponent>(e);
            return mr && mr->ModelData && mr->ModelData->IsSkinned && mr->ModelData->SkeletonData ? mr->ModelData.get() : nullptr;
        }

        void UpdateLeaderPose(Scene& scene)
        {
            auto& reg = scene.m_Registry;
            // İskelet çifti -> izleyen düğüm dizini başına önder düğüm dizini (-1 yok)
            static auto& maps = *new std::map<std::pair<const Skeleton*, const Skeleton*>, std::vector<int>>();
            static thread_local Pose s_Pose;
            for (auto e : reg.view<LeaderPoseComponent>())
            {
                auto& lp = reg.get<LeaderPoseComponent>(e);
                lp.BoneMatrices.clear();
                const Model* follower = SkinnedModel(scene, e);
                Entity leaderEntity = lp.Leader ? scene.FindEntityByUUID(lp.Leader) : Entity{};
                if (!follower || !leaderEntity || !scene.IsEntityActive(e))
                    continue;
                const entt::entity le = (entt::entity)leaderEntity;
                const Model* leader = SkinnedModel(scene, le);
                const Pose* leaderPose = FinalPoseOf(scene, le);
                if (!leader || !leaderPose)
                    continue;
                const Skeleton& fs = *follower->SkeletonData;
                const Skeleton& ls = *leader->SkeletonData;
                const auto& fNodes = Animator::Flatten(fs);
                const auto& lNodes = Animator::Flatten(ls);
                auto key = std::make_pair(&fs, &ls);
                auto it = maps.find(key);
                if (it == maps.end())
                {
                    std::unordered_map<std::string, int> byName;
                    for (int i = 0; i < (int)lNodes.size(); ++i)
                        byName[lNodes[(size_t)i].Node->Name] = i;
                    std::vector<int> map(fNodes.size(), -1);
                    for (size_t i = 0; i < fNodes.size(); ++i)
                        if (auto f = byName.find(fNodes[i].Node->Name); f != byName.end())
                            map[i] = f->second;
                    it = maps.emplace(key, std::move(map)).first;
                }
                // Kendi pozu (yüz animasyonu, blend shape eğrileri) + ortak kemikler önderden
                if (const Pose* own = FinalPoseOf(scene, e); own && own->size() >= fNodes.size())
                    s_Pose = *own;
                else
                    Animator::BindPose(fs, s_Pose);
                int shared = 0;
                for (size_t i = 0; i < fNodes.size() && i < it->second.size(); ++i)
                {
                    const int l = it->second[i];
                    if (l >= 0 && (size_t)l < leaderPose->size())
                    {
                        s_Pose[i] = (*leaderPose)[(size_t)l];
                        ++shared;
                    }
                }
                lp.SharedBones = shared;
                Animator::PoseToSkinning(fs, s_Pose, lp.BoneMatrices);
            }
        }

        void UpdateLiveLinkFace(Scene& scene, float dt)
        {
            auto& reg = scene.m_Registry;
            for (auto e : reg.view<LiveLinkFaceComponent>())
            {
                auto& lf = reg.get<LiveLinkFaceComponent>(e);
                if (!scene.IsEntityActive(e) || lf.Recording.empty())
                    continue;
                const FaceRecording* rec = LoadRecording(SceneSerializer::ResolveAssetPath(lf.Recording), lf.FrameRate);
                if (!rec)
                    continue;
                lf.Duration = rec->Duration;
                lf.Curves = (int)rec->Curves.size();
                if (lf.Playing)
                {
                    lf.Time += dt * lf.Speed;
                    if (lf.Time > rec->Duration)
                        lf.Time = lf.Loop && rec->Duration > 0.0f ? std::fmod(lf.Time, rec->Duration) : rec->Duration;
                    if (lf.Time < 0.0f)
                        lf.Time = 0.0f;
                }
                // Satır arası doğrusal
                auto hi = std::upper_bound(rec->Times.begin(), rec->Times.end(), lf.Time);
                size_t i1 = (size_t)std::min<std::ptrdiff_t>(hi - rec->Times.begin(), (std::ptrdiff_t)rec->Times.size() - 1);
                size_t i0 = i1 > 0 ? i1 - 1 : 0;
                const float span = rec->Times[i1] - rec->Times[i0];
                const float f = span > 1e-6f ? std::clamp((lf.Time - rec->Times[i0]) / span, 0.0f, 1.0f) : 0.0f;
                const auto* names = BlendShapes::Names(scene, (uint32_t)e);
                if (!names)
                {
                    lf.Mapped = 0;
                    continue;
                }
                // Eşleme: Remap tablosu, sonra normalize ad
                std::unordered_map<std::string, std::string> remap;
                {
                    std::stringstream ss(lf.Remap);
                    std::string pair;
                    while (std::getline(ss, pair, ';'))
                    {
                        const size_t eq = pair.find('=');
                        if (eq == std::string::npos) continue;
                        auto trim = [](std::string x) {
                            const size_t a = x.find_first_not_of(" \t"), b = x.find_last_not_of(" \t");
                            return a == std::string::npos ? std::string() : x.substr(a, b - a + 1);
                        };
                        remap[NormalizeShape(trim(pair.substr(0, eq)))] = trim(pair.substr(eq + 1));
                    }
                }
                std::unordered_map<std::string, const std::string*> byNorm;
                for (const auto& n : *names)
                    byNorm.emplace(NormalizeShape(n), &n);
                int mapped = 0;
                for (size_t c = 0; c < rec->Curves.size(); ++c)
                {
                    const std::string norm = NormalizeShape(rec->Curves[c]);
                    std::string target;
                    if (auto r = remap.find(norm); r != remap.end())
                        target = r->second;
                    else if (auto b = byNorm.find(norm); b != byNorm.end())
                        target = *b->second;
                    if (target.empty())
                        continue;
                    const float v = rec->Rows[i0][c] + (rec->Rows[i1][c] - rec->Rows[i0][c]) * f;
                    if (BlendShapes::SetWeight(scene, (uint32_t)e, target, v * lf.Intensity))
                        ++mapped;
                }
                lf.Mapped = mapped;
            }
        }

        void ApplyShading(const Model& model, bool skin, bool hair, const std::string& kind)
        {
            for (const auto& mat : model.Materials)
            {
                if (!mat)
                    continue;
                const std::string name = Lower(mat->Name);
                const bool lash = Has(name, "lash") || Has(name, "brow") || kind == "eyebrows" || kind == "eyelashes";
                const bool eye = Has(name, "eye") && !lash;
                const bool teeth = Has(name, "teeth") || Has(name, "tooth") || Has(name, "tongue");
                if (hair && (kind == "hair" || kind == "beard" || kind == "mustache" || lash || Has(name, "hair") || Has(name, "fuzz")))
                {
                    mat->Shading = Material::ShadingModel::Hair;
                    mat->Rendering = Material::RenderMode::Cutout;
                    mat->AlphaClip = 0.33f;
                    mat->Culling = Material::CullMode::Off;
                }
                else if (skin && (kind == "body" || kind == "face") && !eye && !teeth)
                {
                    mat->Shading = Material::ShadingModel::Subsurface;
                    mat->SubsurfaceColor = glm::vec3(0.85f, 0.32f, 0.22f);
                    mat->SubsurfaceAmount = 0.5f;
                }
            }
        }
    }

    namespace MetaHuman
    {
        const std::vector<std::string>& ARKitShapes()
        {
            static const std::vector<std::string> k = {
                "EyeBlinkLeft", "EyeLookDownLeft", "EyeLookInLeft", "EyeLookOutLeft", "EyeLookUpLeft", "EyeSquintLeft", "EyeWideLeft",
                "EyeBlinkRight", "EyeLookDownRight", "EyeLookInRight", "EyeLookOutRight", "EyeLookUpRight", "EyeSquintRight", "EyeWideRight",
                "JawForward", "JawRight", "JawLeft", "JawOpen", "MouthClose", "MouthFunnel", "MouthPucker", "MouthRight", "MouthLeft",
                "MouthSmileLeft", "MouthSmileRight", "MouthFrownLeft", "MouthFrownRight", "MouthDimpleLeft", "MouthDimpleRight",
                "MouthStretchLeft", "MouthStretchRight", "MouthRollLower", "MouthRollUpper", "MouthShrugLower", "MouthShrugUpper",
                "MouthPressLeft", "MouthPressRight", "MouthLowerDownLeft", "MouthLowerDownRight", "MouthUpperUpLeft", "MouthUpperUpRight",
                "BrowDownLeft", "BrowDownRight", "BrowInnerUp", "BrowOuterUpLeft", "BrowOuterUpRight", "CheekPuff", "CheekSquintLeft",
                "CheekSquintRight", "NoseSneerLeft", "NoseSneerRight", "TongueOut" };
            return k;
        }

        std::string ClassifyPart(const std::string& fileName)
        {
            const std::string n = Lower(fs::path(fileName).stem().string());
            if (Has(n, "eyelash")) return "eyelashes";
            if (Has(n, "eyebrow") || Has(n, "brow")) return "eyebrows";
            if (Has(n, "beard")) return "beard";
            if (Has(n, "mustache") || Has(n, "moustache")) return "mustache";
            if (Has(n, "hair") || Has(n, "groom") || Has(n, "fuzz")) return "hair";
            if (Has(n, "teeth") || Has(n, "tooth")) return "teeth";
            if (Has(n, "eye")) return "eyes";
            if (Has(n, "face") || Has(n, "head")) return "face";
            if (Has(n, "body")) return "body";
            if (Has(n, "top") || Has(n, "bottom") || Has(n, "shoe") || Has(n, "cloth") || Has(n, "outfit") || Has(n, "shirt") || Has(n, "pants"))
                return "clothing";
            return "other";
        }

        int Assemble(Scene& scene, Entity root, std::string& error)
        {
            if (!root || !root.HasComponent<MetaHumanComponent>())
            {
                error = "no MetaHuman component";
                return 0;
            }
            auto& mh = root.GetComponent<MetaHumanComponent>();
            const fs::path folder = SceneSerializer::ResolveAssetPath(mh.Folder);
            std::error_code ec;
            if (mh.Folder.empty() || !fs::is_directory(folder, ec))
            {
                error = "folder not found: " + mh.Folder;
                return 0;
            }
            // Önceki kurulumun parçaları
            for (uint64_t id : mh.PartIds)
                if (Entity old = scene.FindEntityByUUID(id))
                    scene.DestroyEntity(old);
            mh.PartIds.clear();
            mh.Body.clear();
            mh.Face.clear();
            // Parçalar (alt klasörler dahil, ada göre sıralı)
            std::vector<fs::path> files;
            for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
                if (!it->is_regular_file()) continue;
                const std::string ext = Lower(it->path().extension().string());
                if (ext == ".fbx" || ext == ".glb" || ext == ".gltf" || ext == ".obj")
                    files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            if (files.empty())
            {
                error = "no FBX / glTF parts in " + mh.Folder;
                return 0;
            }
            struct Part { Entity E; std::string Kind; std::shared_ptr<Model> M; };
            std::vector<Part> parts;
            for (const auto& file : files)
            {
                auto model = ModelLoader::Load(file.string());
                if (!model)
                {
                    CQ_CORE_WARN("[MetaHuman] cannot load {0}", file.string());
                    continue;
                }
                const std::string kind = ClassifyPart(file.filename().string());
                Entity e = scene.CreateEntity(file.stem().string());
                scene.SetEntityParent(e, root, false);
                auto& tc = e.GetComponent<TransformComponent>();
                tc.Position = glm::vec3(0.0f);
                tc.Rotation = glm::vec3(0.0f);
                tc.Scale = glm::vec3(1.0f);
                auto& mr = e.HasComponent<MeshRendererComponent>() ? e.GetComponent<MeshRendererComponent>() : e.AddComponent<MeshRendererComponent>();
                mr.ModelPath = SceneSerializer::ToAssetPath(file.string());
                mr.ModelData = model;
                ApplyShading(*model, mh.SkinShading, mh.HairShading, kind);
                mh.PartIds.push_back(e.GetComponent<IDComponent>().ID);
                parts.push_back({ e, kind, model });
            }
            // Gövde: "body" adlı (yoksa en çok kemikli) animasyonlu parça; yüz: "face"
            Part* body = nullptr;
            for (auto& p : parts)
                if (p.Kind == "body" && p.M->IsSkinned) { body = &p; break; }
            if (!body)
                for (auto& p : parts)
                    if (p.M->IsSkinned && p.M->SkeletonData && (!body || p.M->SkeletonData->GetBoneCount() > body->M->SkeletonData->GetBoneCount()))
                        body = &p;
            for (auto& p : parts)
            {
                if (p.Kind == "face" && mh.Face.empty())
                    mh.Face = p.E.GetComponent<TagComponent>().Tag;
                if (p.M->IsSkinned && p.M->SkeletonData && !p.M->SkeletonData->MorphNames.empty() && !p.E.HasComponent<BlendShapesComponent>())
                    p.E.AddComponent<BlendShapesComponent>();
                if (body && &p != body && p.M->IsSkinned && p.M->SkeletonData)
                {
                    auto& lp = p.E.HasComponent<LeaderPoseComponent>() ? p.E.GetComponent<LeaderPoseComponent>() : p.E.AddComponent<LeaderPoseComponent>();
                    lp.Leader = body->E.GetComponent<IDComponent>().ID;
                }
            }
            if (body)
                mh.Body = body->E.GetComponent<TagComponent>().Tag;
            mh.Parts = (int)parts.size();
            CQ_CORE_INFO("[MetaHuman] {0}: {1} parts (body '{2}', face '{3}')", mh.Folder, parts.size(), mh.Body, mh.Face);
            return (int)parts.size();
        }

        void Update(Scene& scene, float dt)
        {
            UpdateLeaderPose(scene);
            UpdateLiveLinkFace(scene, dt);
        }
    }

    template<> void Scene::OnComponentAdded<LeaderPoseComponent>(Entity, LeaderPoseComponent&) {}
    template<> void Scene::OnComponentAdded<LiveLinkFaceComponent>(Entity, LiveLinkFaceComponent&) {}
    template<> void Scene::OnComponentAdded<MetaHumanComponent>(Entity, MetaHumanComponent&) {}

    void RegisterMetaHumanComponents()
    {
        const glm::vec4 kColor{ 0.95f, 0.6f, 0.75f, 1.0f };
        {
            using C = LeaderPoseComponent;
            ComponentDef<C>("Leader Pose", "LeaderPoseComponent", "Animation", ICON_FA_PERSON_RAYS).Color(kColor)
                .Describe("This skinned mesh follows the Leader's skeleton pose by bone name (Unreal Set Leader Pose Component): heads, hair "
                          "and clothes move with the body; bones the leader lacks keep this mesh's own animation.")
                .Field("Leader", "Leader", &C::Leader)
                .Field("SharedBones", "Shared Bones", &C::SharedBones).ReadOnly().Header("Runtime");
        }
        {
            using C = LiveLinkFaceComponent;
            ComponentDef<C>("Live Link Face", "LiveLinkFaceComponent", "Animation", ICON_FA_FACE_GRIN_WIDE).Color(kColor)
                .Describe("Plays a Live Link Face (iPhone ARKit) CSV recording on this mesh's blend shapes: the 52 ARKit curves "
                          "(eyeBlinkLeft, jawOpen, mouthSmileLeft ...) drive shapes of the same name, or names given in Remap.")
                .Field("Recording", "Recording", &C::Recording, FieldKind::Asset).Filter(".csv")
                .Field("Playing", "Playing", &C::Playing)
                .Field("Loop", "Loop", &C::Loop)
                .Field("Speed", "Speed", &C::Speed).Range(0.0f, 10.0f, 0.01f)
                .Field("Intensity", "Intensity", &C::Intensity).Range(0.0f, 2.0f, 0.01f)
                .Field("FrameRate", "Timecode Frame Rate", &C::FrameRate).Range(1.0f, 240.0f, 1.0f)
                .Field("Remap", "Remap (ARKit=Shape; ...)", &C::Remap)
                .Field("Time", "Time", &C::Time).ReadOnly().Header("Runtime")
                .Field("Duration", "Duration", &C::Duration).ReadOnly()
                .Field("Curves", "Curves", &C::Curves).ReadOnly()
                .Field("Mapped", "Mapped", &C::Mapped).ReadOnly();
        }
        {
            using C = MetaHumanComponent;
            ComponentDef<C>("MetaHuman", "MetaHumanComponent", "Animation", ICON_FA_PERSON).Color(kColor)
                .Describe("Assembles a MetaHuman export (FBX / glTF parts in a folder: body, face, hair, eyebrows, eyelashes, beard, eyes, "
                          "teeth, clothes) into this character: parts follow the body with Leader Pose, skin uses Subsurface and hair the Hair "
                          "shading model; add Live Link Face to the face for ARKit facial capture.")
                .Field("Folder", "Export Folder", &C::Folder)
                .Field("SkinShading", "Skin Shading", &C::SkinShading)
                .Field("HairShading", "Hair Shading", &C::HairShading)
                .Action("Assemble", [](Entity e, C&) {
                    std::string err;
                    if (e.GetScene() && MetaHuman::Assemble(*e.GetScene(), e, err) == 0)
                        CQ_CORE_WARN("[MetaHuman] {0}", err);
                })
                .Field("Parts", "Parts", &C::Parts).ReadOnly().Header("Runtime")
                .Field("Body", "Body", &C::Body).ReadOnly()
                .Field("Face", "Face", &C::Face).ReadOnly()
                .CustomData(
                    [](YAML::Emitter& out, C& c) {
                        if (c.PartIds.empty()) return;
                        out << YAML::Key << "PartIds" << YAML::Value << YAML::Flow << YAML::BeginSeq;
                        for (uint64_t id : c.PartIds) out << id;
                        out << YAML::EndSeq;
                    },
                    [](const YAML::Node& n, C& c) {
                        c.PartIds.clear();
                        if (auto ids = n["PartIds"]; ids && ids.IsSequence())
                            for (const auto& id : ids) c.PartIds.push_back(id.as<uint64_t>());
                    });
        }
    }
}
