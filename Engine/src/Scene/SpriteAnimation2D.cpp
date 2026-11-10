#include "SpriteAnimation2D.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Skeleton2D.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Utils/Crypto.h"
#include "Core/Logging/Log.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/SpriteSheet.h"
#include "Renderer/RHI/Texture.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        constexpr const char* kLibraryKind = "SpriteLibrary";

        std::shared_ptr<Texture2D> TextureOf(const std::string& path)
        {
            static auto& cache = *new std::unordered_map<std::string, std::shared_ptr<Texture2D>>();
            auto& t = cache[path];
            if (!t && !path.empty())
                t = Texture2D::Create(SceneSerializer::ResolveAssetPath(path));
            return t;
        }

        // Kütüphane tablosu: Kategori, Etiket, Sprite (doku), Sprite Adı (sayfa dilimi, isteğe bağlı)
        const std::vector<std::vector<std::string>>* Entries(const std::string& asset, SimpleAssets::Data& data)
        {
            if (asset.empty() || !SimpleAssets::Load(asset, data))
                return nullptr;
            return &data.Table("Entries");
        }

        // Kökten ad yoluyla kemik ("" = kök, "Kol/Ön Kol")
        Entity ResolvePath(Entity root, const std::string& path)
        {
            Entity cur = root;
            size_t start = 0;
            while (cur && start < path.size())
            {
                size_t slash = path.find('/', start);
                const std::string name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                Entity next{};
                for (Entity c : cur.GetChildren())
                    if (c.GetComponent<TagComponent>().Tag == name) { next = c; break; }
                cur = next;
                if (slash == std::string::npos)
                    break;
                start = slash + 1;
            }
            return cur;
        }

        float SegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
        {
            const glm::vec2 ab = b - a;
            const float len2 = glm::dot(ab, ab);
            const float t = len2 > 1e-10f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
            return glm::length(p - (a + ab * t));
        }

        // İkili bağlama verisi (sahne YAML'ında base64)
        std::string PackSkin(const SpriteSkinComponent& s)
        {
            std::string out;
            auto put = [&](const void* p, size_t n) { out.append((const char*)p, n); };
            const uint32_t version = 2, bones = (uint32_t)s.BonePaths.size(), verts = (uint32_t)s.Vertices.size();
            put(&version, 4);
            put(&bones, 4);
            for (uint32_t i = 0; i < bones; ++i)
            {
                const uint32_t len = (uint32_t)s.BonePaths[i].size();
                put(&len, 4);
                put(s.BonePaths[i].data(), len);
                put(&s.BindInverse[i], sizeof(glm::mat4));
            }
            put(&verts, 4);
            const int32_t gx = s.BoundGridX, gy = s.BoundGridY;
            put(&gx, 4);
            put(&gy, 4);
            for (uint32_t i = 0; i < verts; ++i)
            {
                put(&s.Vertices[i], sizeof(glm::vec2));
                put(&s.UVs[i], sizeof(glm::vec2));
                put(&s.BoneIndex[i], sizeof(glm::ivec4));
                put(&s.BoneWeight[i], sizeof(glm::vec4));
            }
            const uint32_t indices = (uint32_t)s.Indices.size();
            put(&indices, 4);
            if (indices)
                put(s.Indices.data(), indices * sizeof(uint32_t));
            return out;
        }

        bool UnpackSkin(const std::string& data, SpriteSkinComponent& s)
        {
            size_t at = 0;
            auto get = [&](void* p, size_t n) { if (at + n > data.size()) return false; std::memcpy(p, data.data() + at, n); at += n; return true; };
            uint32_t version = 0, bones = 0, verts = 0;
            if (!get(&version, 4) || (version != 1 && version != 2) || !get(&bones, 4) || bones > 4096)
                return false;
            SpriteSkinComponent t;
            for (uint32_t i = 0; i < bones; ++i)
            {
                uint32_t len = 0;
                if (!get(&len, 4) || len > 4096)
                    return false;
                std::string path(len, '\0');
                glm::mat4 m(1.0f);
                if (!get(path.data(), len) || !get(&m, sizeof(glm::mat4)))
                    return false;
                t.BonePaths.push_back(std::move(path));
                t.BindInverse.push_back(m);
            }
            int32_t gx = 0, gy = 0;
            if (!get(&verts, 4) || verts > 1u << 20 || !get(&gx, 4) || !get(&gy, 4))
                return false;
            t.Vertices.resize(verts);
            t.UVs.resize(verts);
            t.BoneIndex.resize(verts);
            t.BoneWeight.resize(verts);
            for (uint32_t i = 0; i < verts; ++i)
                if (!get(&t.Vertices[i], sizeof(glm::vec2)) || !get(&t.UVs[i], sizeof(glm::vec2)) || !get(&t.BoneIndex[i], sizeof(glm::ivec4)) ||
                    !get(&t.BoneWeight[i], sizeof(glm::vec4)))
                    return false;
            if (version >= 2)
            {
                uint32_t indices = 0;
                if (!get(&indices, 4) || indices > 1u << 24)
                    return false;
                t.Indices.resize(indices);
                if (indices && !get(t.Indices.data(), indices * sizeof(uint32_t)))
                    return false;
            }
            s.Indices = std::move(t.Indices);
            s.BonePaths = std::move(t.BonePaths);
            s.BindInverse = std::move(t.BindInverse);
            s.Vertices = std::move(t.Vertices);
            s.UVs = std::move(t.UVs);
            s.BoneIndex = std::move(t.BoneIndex);
            s.BoneWeight = std::move(t.BoneWeight);
            s.BoundGridX = gx;
            s.BoundGridY = gy;
            s.BoundBones = (int)s.BonePaths.size();
            s.BoundVertices = (int)s.Vertices.size();
            return true;
        }
    }

    namespace SpriteAnimation2D
    {
        std::string FindLibraryAsset(Scene& scene, entt::entity entity)
        {
            for (Entity e(entity, &scene); e; e = e.GetParent())
                if (e.HasComponent<SpriteLibraryComponent>() && !e.GetComponent<SpriteLibraryComponent>().Asset.empty())
                    return e.GetComponent<SpriteLibraryComponent>().Asset;
            return {};
        }

        std::vector<std::string> Categories(const std::string& libraryAsset)
        {
            std::vector<std::string> out;
            SimpleAssets::Data data;
            if (const auto* rows = Entries(libraryAsset, data))
                for (const auto& row : *rows)
                {
                    const std::string c = SimpleAssets::Cell(row, 0);
                    if (!c.empty() && std::find(out.begin(), out.end(), c) == out.end())
                        out.push_back(c);
                }
            return out;
        }

        std::vector<std::string> Labels(const std::string& libraryAsset, const std::string& category)
        {
            std::vector<std::string> out;
            SimpleAssets::Data data;
            if (const auto* rows = Entries(libraryAsset, data))
                for (const auto& row : *rows)
                    if (SimpleAssets::Cell(row, 0) == category && !SimpleAssets::Cell(row, 1).empty())
                        out.push_back(SimpleAssets::Cell(row, 1));
            return out;
        }

        void Update(Scene& scene)
        {
            auto view = scene.m_Registry.view<SpriteResolverComponent, SpriteRendererComponent>();
            for (auto e : view)
            {
                auto& r = view.get<SpriteResolverComponent>(e);
                const std::string lib = FindLibraryAsset(scene, e);
                if (lib.empty())
                    continue;
                SimpleAssets::Data data;
                const auto* rows = Entries(lib, data);
                if (!rows)
                    continue;
                // Label Index (canlandırılabilir): kategorideki sıradan etiketi seçer
                if (r.LabelIndex >= 0 && r.LabelIndex != r.AppliedIndex)
                {
                    int i = 0;
                    for (const auto& row : *rows)
                        if (SimpleAssets::Cell(row, 0) == r.Category)
                        {
                            if (i++ == r.LabelIndex) { r.Label = SimpleAssets::Cell(row, 1); break; }
                        }
                    r.AppliedIndex = r.LabelIndex;
                }
                for (const auto& row : *rows)
                {
                    if (SimpleAssets::Cell(row, 0) != r.Category || SimpleAssets::Cell(row, 1) != r.Label)
                        continue;
                    const std::string tex = SimpleAssets::Cell(row, 2), name = SimpleAssets::Cell(row, 3);
                    const std::string key = lib + "|" + r.Category + "|" + r.Label + "|" + tex + "|" + name;
                    auto& sprite = view.get<SpriteRendererComponent>(e);
                    if (key != r.AppliedKey || !sprite.Texture)
                    {
                        if (auto t = TextureOf(tex))
                        {
                            sprite.Texture = t;
                            sprite.SpriteName = name;
                            if (name.empty())
                                sprite.Rect = glm::vec4(0.0f);
                            Renderer2D::ApplySpriteImport(sprite);
                            r.AppliedKey = key;
                        }
                    }
                    break;
                }
            }
        }

        // Sprite'ın (seçili dilim, yoksa kemikli ilk dilim) Skinning Editor verisi
        const SpriteDef* SliceWithSkin(const SpriteRendererComponent& sprite)
        {
            if (!sprite.Texture || sprite.Texture->GetPath().empty())
                return nullptr;
            auto sheet = SpriteSheet::Get(sprite.Texture->GetPath());
            if (!sheet)
                return nullptr;
            auto valid = [](const SpriteDef& d) { return !d.Skin.Bones.empty() && !d.Skin.Vertices.empty() && d.Skin.BoneIndex.size() == d.Skin.Vertices.size(); };
            if (!sprite.SpriteName.empty())
            {
                const SpriteDef* d = sheet->Find(sprite.SpriteName);
                return d && valid(*d) ? d : nullptr;
            }
            for (const auto& d : sheet->Sprites)
                if (valid(d))
                    return &d;
            return nullptr;
        }

        bool CreateBones(Scene& scene, entt::entity entity, std::string& error)
        {
            Entity e(entity, &scene);
            if (!e || !e.HasComponent<SpriteSkinComponent>() || !e.HasComponent<SpriteRendererComponent>())
            {
                error = "Sprite Skin needs a Sprite Renderer on the same object.";
                return false;
            }
            auto& sprite = e.GetComponent<SpriteRendererComponent>();
            const SpriteDef* def = SliceWithSkin(sprite);
            if (!def)
            {
                error = "The sprite has no bones and weights yet: open Sprite Editor > Skinning Editor.";
                return false;
            }
            glm::vec2 mn, mx;
            if (!Renderer2D::GetSpriteLocalBounds(sprite, mn, mx))
            {
                error = "The sprite has no size (assign a texture).";
                return false;
            }
            auto& skin = e.GetComponent<SpriteSkinComponent>();
            // Önceki kurulumun kökü
            if (Entity old = skin.RootBone ? scene.FindEntityByUUID(skin.RootBone) : Entity{})
                if (old.GetParent() == e && old.GetComponent<TagComponent>().Tag == "Bones")
                    scene.DestroyEntity(old);
            const glm::vec2 rect(std::max(def->Rect.z, 1.0f), std::max(def->Rect.w, 1.0f));
            auto toLocal = [&](const glm::vec2& px) { return mn + (mx - mn) * (px / rect); };
            Entity root = scene.CreateEntity("Bones");
            scene.SetEntityParent(root, e, false);
            auto& rt = root.GetComponent<TransformComponent>();
            rt.Position = glm::vec3(0.0f);
            rt.Rotation = glm::vec3(0.0f);
            rt.Scale = glm::vec3(1.0f);
            const auto& bones = def->Skin.Bones;
            std::vector<Entity> created(bones.size());
            std::vector<float> angles(bones.size(), 0.0f);
            std::function<void(int)> make = [&](int i) {
                if (created[(size_t)i])
                    return;
                const SpriteBone& b = bones[(size_t)i];
                const glm::vec2 a = toLocal(b.Position), tip = toLocal(b.Tip);
                const float angle = std::atan2(tip.y - a.y, tip.x - a.x);
                angles[(size_t)i] = angle;
                Entity parent = root;
                glm::vec2 local = a;
                float parentAngle = 0.0f;
                if (b.Parent >= 0 && b.Parent < (int)bones.size() && b.Parent != i)
                {
                    make(b.Parent);
                    parent = created[(size_t)b.Parent];
                    parentAngle = angles[(size_t)b.Parent];
                    const glm::vec2 d = a - toLocal(bones[(size_t)b.Parent].Position);
                    local = glm::vec2(d.x * std::cos(-parentAngle) - d.y * std::sin(-parentAngle), d.x * std::sin(-parentAngle) + d.y * std::cos(-parentAngle));
                }
                Entity be = scene.CreateEntity(b.Name);
                scene.SetEntityParent(be, parent, false);
                auto& t = be.GetComponent<TransformComponent>();
                t.Position = glm::vec3(local, 0.0f);
                t.Rotation = glm::vec3(0.0f, 0.0f, angle - parentAngle);
                t.Scale = glm::vec3(1.0f);
                be.AddComponent<Bone2DComponent>().Length = glm::length(tip - a);
                created[(size_t)i] = be;
            };
            for (int i = 0; i < (int)bones.size(); ++i)
                make(i);
            skin.RootBone = root.GetComponent<IDComponent>().ID;
            return Bind(scene, entity, error);
        }

        void Unbind(SpriteSkinComponent& s)
        {
            s.BonePaths.clear();
            s.BindInverse.clear();
            s.Vertices.clear();
            s.UVs.clear();
            s.BoneIndex.clear();
            s.BoneWeight.clear();
            s.Indices.clear();
            s.BoundGridX = s.BoundGridY = 0;
            s.BoundBones = s.BoundVertices = 0;
        }

        bool Bind(Scene& scene, entt::entity entity, std::string& error)
        {
            Entity e(entity, &scene);
            if (!e || !e.HasComponent<SpriteSkinComponent>() || !e.HasComponent<SpriteRendererComponent>())
            {
                error = "Sprite Skin needs a Sprite Renderer on the same object.";
                return false;
            }
            auto& skin = e.GetComponent<SpriteSkinComponent>();
            Entity root = skin.RootBone ? scene.FindEntityByUUID(skin.RootBone) : Entity{};
            if (!root)
            {
                error = "Set the Root Bone (the top Bone 2D of the skeleton).";
                return false;
            }
            scene.UpdateWorldTransforms();
            // Kemikler: kök + altındaki Bone 2D'ler (derinlik öncelikli)
            struct BoneInfo { std::string Path; glm::mat4 World; glm::vec2 A, B; };
            std::vector<BoneInfo> bones;
            std::function<void(Entity, const std::string&)> walk = [&](Entity b, const std::string& path) {
                if (b == root || b.HasComponent<Bone2DComponent>())
                {
                    const glm::mat4 w = scene.ComputeWorldMatrix((entt::entity)b);
                    const float len = b.HasComponent<Bone2DComponent>() ? b.GetComponent<Bone2DComponent>().Length : 0.0f;
                    bones.push_back({ path, w, glm::vec2(w[3]), glm::vec2(w * glm::vec4(len, 0.0f, 0.0f, 1.0f)) });
                }
                for (Entity c : b.GetChildren())
                {
                    if ((entt::entity)c == entity)
                        continue;
                    const std::string name = c.GetComponent<TagComponent>().Tag;
                    walk(c, path.empty() ? name : path + "/" + name);
                }
            };
            walk(root, "");
            if (bones.empty())
            {
                error = "No bones found under the Root Bone.";
                return false;
            }
            auto& sprite = e.GetComponent<SpriteRendererComponent>();
            glm::vec2 mn, mx;
            if (!Renderer2D::GetSpriteLocalBounds(sprite, mn, mx))
            {
                error = "The sprite has no size (assign a texture).";
                return false;
            }
            const glm::mat4 spriteWorld = scene.ComputeWorldMatrix(entity);
            Unbind(skin);
            // Skinning Editor verisi varsa onun ağı ve ağırlıkları kullanılır (kemikler ada göre eşlenir)
            if (const SpriteDef* def = SliceWithSkin(sprite))
            {
                const auto& data = def->Skin;
                std::vector<int> map(data.Bones.size(), -1);
                for (size_t b = 0; b < data.Bones.size(); ++b)
                    for (size_t i = 0; i < bones.size(); ++i)
                    {
                        const size_t cut = bones[i].Path.find_last_of('/');
                        const std::string leaf = cut == std::string::npos ? bones[i].Path : bones[i].Path.substr(cut + 1);
                        if (leaf == data.Bones[b].Name)
                            map[b] = (int)i;
                    }
                if (std::find(map.begin(), map.end(), -1) == map.end())
                {
                    for (const auto& b : bones)
                    {
                        skin.BonePaths.push_back(b.Path);
                        skin.BindInverse.push_back(glm::inverse(b.World) * spriteWorld);
                    }
                    const glm::vec2 rect(std::max(def->Rect.z, 1.0f), std::max(def->Rect.w, 1.0f));
                    for (size_t v = 0; v < data.Vertices.size(); ++v)
                    {
                        const glm::vec2 f = data.Vertices[v] / rect;
                        skin.Vertices.push_back(mn + (mx - mn) * f);
                        skin.UVs.push_back(f);
                        glm::ivec4 idx(-1);
                        for (int k = 0; k < 4; ++k)
                            if (data.BoneIndex[v][k] >= 0 && data.BoneIndex[v][k] < (int)map.size())
                                idx[k] = map[(size_t)data.BoneIndex[v][k]];
                        skin.BoneIndex.push_back(idx);
                        skin.BoneWeight.push_back(data.BoneWeight[v]);
                    }
                    skin.Indices = data.Indices;
                    skin.BoundGridX = skin.BoundGridY = 1;
                    skin.BoundBones = (int)skin.BonePaths.size();
                    skin.BoundVertices = (int)skin.Vertices.size();
                    return true;
                }
            }
            const int gx = std::clamp(skin.GridX, 1, 64), gy = std::clamp(skin.GridY, 1, 64);
            skin.BoundGridX = gx;
            skin.BoundGridY = gy;
            for (const auto& b : bones)
            {
                skin.BonePaths.push_back(b.Path);
                skin.BindInverse.push_back(glm::inverse(b.World) * spriteWorld);
            }
            for (int y = 0; y <= gy; ++y)
                for (int x = 0; x <= gx; ++x)
                {
                    const glm::vec2 f((float)x / (float)gx, (float)y / (float)gy);
                    const glm::vec2 v = mn + (mx - mn) * f;
                    const glm::vec2 p = glm::vec2(spriteWorld * glm::vec4(v, 0.0f, 1.0f));
                    // Otomatik ağırlık: kemik parçasına uzaklığın ters 4. kuvveti, en yakın 4 kemik
                    std::vector<std::pair<float, int>> w;
                    for (int i = 0; i < (int)bones.size(); ++i)
                    {
                        const float d = SegmentDistance(p, bones[(size_t)i].A, bones[(size_t)i].B);
                        w.push_back({ 1.0f / std::pow(d + 0.02f, 4.0f), i });
                    }
                    std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                    glm::ivec4 idx(-1);
                    glm::vec4 wt(0.0f);
                    float total = 0.0f;
                    for (int k = 0; k < 4 && k < (int)w.size(); ++k)
                    {
                        idx[k] = w[(size_t)k].second;
                        wt[k] = w[(size_t)k].first;
                        total += wt[k];
                    }
                    if (total > 0.0f)
                        wt /= total;
                    skin.Vertices.push_back(v);
                    skin.UVs.push_back(f);
                    skin.BoneIndex.push_back(idx);
                    skin.BoneWeight.push_back(wt);
                }
            skin.BoundBones = (int)skin.BonePaths.size();
            skin.BoundVertices = (int)skin.Vertices.size();
            return true;
        }

        bool DrawSkinned(Scene& scene, entt::entity entity, SpriteRendererComponent& sprite, const glm::vec4& color)
        {
            auto* skin = scene.m_Registry.try_get<SpriteSkinComponent>(entity);
            if (!skin || skin->BonePaths.empty() || skin->BoundGridX <= 0 || skin->BoundGridY <= 0 ||
                (skin->Indices.empty() && (int)skin->Vertices.size() != (skin->BoundGridX + 1) * (skin->BoundGridY + 1)))
                return false;
            Entity root = skin->RootBone ? scene.FindEntityByUUID(skin->RootBone) : Entity{};
            if (!root)
                return false;
            // Kemiklerin şimdiki dünya matrisi * bağlama tersi
            std::vector<glm::mat4> m(skin->BonePaths.size(), glm::mat4(1.0f));
            for (size_t i = 0; i < skin->BonePaths.size(); ++i)
            {
                Entity b = skin->BonePaths[i].empty() ? root : ResolvePath(root, skin->BonePaths[i]);
                if (!b)
                    return false;
                m[i] = scene.ComputeWorldMatrix((entt::entity)b) * skin->BindInverse[i];
            }
            std::shared_ptr<Texture2D> tex;
            glm::vec2 uvMin, uvMax;
            if (!Renderer2D::GetSpriteUV(sprite, tex, uvMin, uvMax))
                return false;
            const size_t n = skin->Vertices.size();
            std::vector<glm::vec3> pos(n);
            for (size_t v = 0; v < n; ++v)
            {
                const glm::vec4 p(skin->Vertices[v], 0.0f, 1.0f);
                glm::vec4 acc(0.0f);
                float tw = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    const int bi = skin->BoneIndex[v][k];
                    const float w = skin->BoneWeight[v][k];
                    if (bi < 0 || bi >= (int)m.size() || w <= 0.0f)
                        continue;
                    acc += (m[(size_t)bi] * p) * w;
                    tw += w;
                }
                pos[v] = tw > 0.0f ? glm::vec3(acc) / tw : glm::vec3(scene.ComputeWorldMatrix(entity) * p);
            }
            const int gx = skin->BoundGridX, gy = skin->BoundGridY;
            auto uvAt = [&](size_t v) { return uvMin + (uvMax - uvMin) * skin->UVs[v]; };
            if (!skin->Indices.empty())
            {
                for (size_t t = 0; t + 2 < skin->Indices.size(); t += 3)
                {
                    const uint32_t a = skin->Indices[t], b = skin->Indices[t + 1], c = skin->Indices[t + 2];
                    if (a >= n || b >= n || c >= n)
                        continue;
                    const glm::vec3 corners[4] = { pos[a], pos[b], pos[c], pos[c] };
                    const glm::vec2 uvs[4] = { uvAt(a), uvAt(b), uvAt(c), uvAt(c) };
                    Renderer2D::DrawQuadCorners(corners, uvs, tex, color);
                }
                return true;
            }
            for (int y = 0; y < gy; ++y)
                for (int x = 0; x < gx; ++x)
                {
                    const size_t i00 = (size_t)(y * (gx + 1) + x), i10 = i00 + 1, i01 = i00 + (size_t)(gx + 1), i11 = i01 + 1;
                    const glm::vec3 corners[4] = { pos[i00], pos[i10], pos[i11], pos[i01] };
                    const glm::vec2 uvs[4] = { uvAt(i00), uvAt(i10), uvAt(i11), uvAt(i01) };
                    Renderer2D::DrawQuadCorners(corners, uvs, tex, color);
                }
            return true;
        }
    }

    template<> void Scene::OnComponentAdded<SpriteLibraryComponent>(Entity, SpriteLibraryComponent&) {}
    template<> void Scene::OnComponentAdded<SpriteResolverComponent>(Entity, SpriteResolverComponent&) {}
    template<> void Scene::OnComponentAdded<SpriteSkinComponent>(Entity, SpriteSkinComponent&) {}

    void RegisterSpriteAnimation2DComponents()
    {
        const glm::vec4 kColor{ 0.55f, 0.85f, 1.0f, 1.0f };
        {
            using C = SpriteLibraryComponent;
            ComponentDef<C>("Sprite Library", "SpriteLibraryComponent", "2D Animation", ICON_FA_IMAGES).Color(kColor)
                .Describe("Gives this object and its children a Sprite Library Asset: named sprites grouped by category. "
                          "Sprite Resolvers below pick sprites from it by category and label.")
                .Field("Asset", "Sprite Library Asset", &C::Asset, FieldKind::Asset).Filter(".cqspritelib");
        }
        {
            using C = SpriteResolverComponent;
            ComponentDef<C>("Sprite Resolver", "SpriteResolverComponent", "2D Animation", ICON_FA_SHUFFLE).Color(kColor)
                .Requires("SpriteRendererComponent")
                .Describe("Sets the Sprite Renderer's sprite from the nearest Sprite Library by Category and Label. Change the label to swap "
                          "the sprite (outfits, faces); animate Label Index for frame-by-frame animation.")
                .Field("Category", "Category", &C::Category)
                .Field("Label", "Label", &C::Label)
                .Field("LabelIndex", "Label Index", &C::LabelIndex).Range(-1.0f, 1000.0f, 1.0f)
                    .Tooltip("0, 1, 2 ... picks the label at that position in the category (for animation). -1 uses Label.");
        }
        {
            using C = SpriteSkinComponent;
            ComponentDef<C>("Sprite Skin", "SpriteSkinComponent", "2D Animation", ICON_FA_PERSON_RAYS).Color(kColor)
                .Requires("SpriteRendererComponent")
                .Describe("Bends the sprite with a 2D skeleton (Bone 2D objects). Set the Root Bone, pose the bones in their rest pose and press "
                          "Bind Bones: the sprite is split into a grid whose points follow the nearest bones.")
                .Field("RootBone", "Root Bone", &C::RootBone)
                .Field("GridX", "Grid Columns", &C::GridX).Range(1.0f, 64.0f, 1.0f)
                .Field("GridY", "Grid Rows", &C::GridY).Range(1.0f, 64.0f, 1.0f)
                .Action("Create Bones", [](Entity e, C&) {
                    std::string err;
                    if (e.GetScene() && !SpriteAnimation2D::CreateBones(*e.GetScene(), (entt::entity)e, err))
                        CQ_CORE_WARN("Sprite Skin: {0}", err);
                })
                .Action("Bind Bones", [](Entity e, C&) {
                    std::string err;
                    if (e.GetScene() && !SpriteAnimation2D::Bind(*e.GetScene(), (entt::entity)e, err))
                        CQ_CORE_WARN("Sprite Skin: {0}", err);
                })
                .Action("Unbind", [](Entity, C& c) { SpriteAnimation2D::Unbind(c); })
                .Field("BoundBones", "Bound Bones", &C::BoundBones).ReadOnly()
                .Field("BoundVertices", "Bound Vertices", &C::BoundVertices).ReadOnly()
                .CustomData(
                    [](YAML::Emitter& out, C& c) {
                        if (!c.BonePaths.empty())
                            out << YAML::Key << "SkinData" << YAML::Value << Crypto::Base64Encode(PackSkin(c));
                    },
                    [](const YAML::Node& n, C& c) {
                        if (auto d = n["SkinData"])
                            UnpackSkin(Crypto::Base64Decode(d.as<std::string>()), c);
                    });
        }
    }
}
