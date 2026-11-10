#include "PrefabSerializer.h"
#include "SceneSerializer.h"
#include "Scene/Components.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"

#include "Core/Math/Math.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        namespace fs = std::filesystem;

        // ------------------------------------------------------------------ Prefab Variant
        // Variant dosyası yalnız tabandan farkları saklar (Unity Prefab Variant):
        //   PrefabVariant:
        //     Base: Assets/Prefabs/Enemy.cqprefab
        //     Overrides: [ { Key: "Enemy/Body", Components: { MeshRendererComponent: {...} }, Removed: [..] } ]
        //     Added: [ { ParentKey: "Enemy", Entity: <varlık düğümü> } ]   (eklenen alt ağaçlar; içteki ebeveynler Parent: UUID)
        //     RemovedEntities: [ "Enemy/Sparkle" ]
        // Varlıklar hiyerarşideki ad yoluyla eşlenir ("Kök/Çocuk", aynı adlı kardeşler "Ad#2").
        // Yükleme: taban (o da variant olabilir) tam YAML'a açılır, farklar uygulanır.

        bool IsVariantFile(const fs::path& path)
        {
            std::ifstream f(path);
            std::string line;
            while (std::getline(f, line))
            {
                if (line.empty() || line[0] == '#')
                    continue;
                return line.rfind("PrefabVariant:", 0) == 0;
            }
            return false;
        }

        fs::path ResolveIn(const std::string& p);

        // Varlık anahtarları: UUID -> "Kök/Çocuk#n"
        struct KeyIndex
        {
            std::map<uint64_t, std::string> KeyOf;
            std::map<std::string, uint64_t> IdOf;
            std::map<uint64_t, uint64_t> ParentOf;
            std::map<uint64_t, std::vector<uint64_t>> ChildrenOf;
            std::vector<uint64_t> Order;   // Entities sırası
        };

        KeyIndex BuildKeys(const YAML::Node& doc)
        {
            KeyIndex k;
            std::map<uint64_t, std::string> tag;
            if (doc["Entities"])
                for (const auto& e : doc["Entities"])
                {
                    const uint64_t id = e["Entity"].as<uint64_t>();
                    k.Order.push_back(id);
                    tag[id] = e["TagComponent"] && e["TagComponent"]["Tag"] ? e["TagComponent"]["Tag"].as<std::string>() : std::string("Entity");
                }
            if (doc["Hierarchy"])
                for (const auto& r : doc["Hierarchy"])
                {
                    const uint64_t c = r["Child"].as<uint64_t>(), par = r["Parent"].as<uint64_t>();
                    k.ParentOf[c] = par;
                    k.ChildrenOf[par].push_back(c);
                }
            std::function<void(uint64_t, const std::string&)> walk = [&](uint64_t id, const std::string& prefix) {
                std::map<std::string, int> seen;
                for (uint64_t c : k.ChildrenOf[id])
                {
                    const std::string& t = tag[c];
                    const int n = ++seen[t];
                    const std::string key = prefix + "/" + (n > 1 ? t + "#" + std::to_string(n) : t);
                    k.KeyOf[c] = key;
                    k.IdOf[key] = c;
                    walk(c, key);
                }
            };
            // Prefab'ın tek kökü adından bağımsız "." (variant kökü yeniden adlandırılabilir)
            std::map<std::string, int> seenRoot;
            bool firstRoot = true;
            for (uint64_t id : k.Order)
                if (!k.ParentOf.count(id))
                {
                    const std::string& t = tag[id];
                    const int n = ++seenRoot[t];
                    const std::string key = firstRoot ? std::string(".") : (n > 1 ? t + "#" + std::to_string(n) : t);
                    firstRoot = false;
                    k.KeyOf[id] = key;
                    k.IdOf[key] = id;
                    walk(id, key);
                }
            return k;
        }

        // Düğümdeki UUID değerlerini eşlemeye göre değiştir (varlık referansları)
        void RemapIds(YAML::Node node, const std::map<uint64_t, uint64_t>& map)
        {
            if (map.empty())
                return;
            if (node.IsScalar())
            {
                try
                {
                    const std::string str = node.Scalar();
                    if (!str.empty() && std::all_of(str.begin(), str.end(), [](char c) { return c >= '0' && c <= '9'; }) && str.size() >= 6)
                        if (auto it = map.find(std::stoull(str)); it != map.end())
                            node = it->second;
                }
                catch (...) {}
                return;
            }
            if (node.IsSequence())
                for (size_t i = 0; i < node.size(); ++i)
                    RemapIds(node[i], map);
            else if (node.IsMap())
                for (auto it = node.begin(); it != node.end(); ++it)
                    RemapIds(it->second, map);
        }

        std::string LoadResolvedText(const fs::path& path, int depth = 0);

        // Variant dosyasını tam sahne YAML metnine aç
        std::string ExpandVariant(const YAML::Node& v, int depth)
        {
            const fs::path basePath = ResolveIn(v["Base"].as<std::string>());
            const std::string baseText = LoadResolvedText(basePath, depth + 1);
            if (baseText.empty())
                return {};
            YAML::Node doc = YAML::Load(baseText);
            KeyIndex keys = BuildKeys(doc);

            // Silinen varlıklar (alt ağaçlarıyla)
            std::set<uint64_t> removed;
            if (v["RemovedEntities"])
                for (const auto& r : v["RemovedEntities"])
                    if (auto it = keys.IdOf.find(r.as<std::string>()); it != keys.IdOf.end())
                    {
                        std::function<void(uint64_t)> mark = [&](uint64_t id) {
                            removed.insert(id);
                            for (uint64_t c : keys.ChildrenOf[id])
                                mark(c);
                        };
                        mark(it->second);
                    }

            // Bileşen farkları
            std::map<uint64_t, YAML::Node> byId;
            YAML::Node entities = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& e : doc["Entities"])
            {
                const uint64_t id = e["Entity"].as<uint64_t>();
                if (removed.count(id))
                    continue;
                YAML::Node copy = YAML::Clone(e);
                byId[id] = copy;
                entities.push_back(copy);
            }
            if (v["Overrides"])
                for (const auto& o : v["Overrides"])
                {
                    auto it = keys.IdOf.find(o["Key"].as<std::string>());
                    if (it == keys.IdOf.end() || !byId.count(it->second))
                        continue;
                    YAML::Node target = byId[it->second];
                    if (o["Components"])
                        for (auto c = o["Components"].begin(); c != o["Components"].end(); ++c)
                            target[c->first.as<std::string>()] = YAML::Clone(c->second);
                    if (o["Removed"])
                        for (const auto& r : o["Removed"])
                            target.remove(r.as<std::string>());
                }

            // Hiyerarşi (silinenler hariç) + eklenen alt ağaçlar
            YAML::Node hierarchy = YAML::Node(YAML::NodeType::Sequence);
            if (doc["Hierarchy"])
                for (const auto& r : doc["Hierarchy"])
                    if (!removed.count(r["Child"].as<uint64_t>()) && !removed.count(r["Parent"].as<uint64_t>()))
                        hierarchy.push_back(YAML::Clone(r));
            if (v["Added"])
                for (const auto& a : v["Added"])
                {
                    YAML::Node ent = YAML::Clone(a["Entity"]);
                    entities.push_back(ent);
                    const uint64_t id = ent["Entity"].as<uint64_t>();
                    uint64_t parent = 0;
                    if (a["ParentKey"])
                    {
                        if (auto it = keys.IdOf.find(a["ParentKey"].as<std::string>()); it != keys.IdOf.end())
                            parent = it->second;
                    }
                    else if (a["Parent"])
                        parent = a["Parent"].as<uint64_t>();
                    if (parent)
                    {
                        YAML::Node r;
                        r["Child"] = id;
                        r["Parent"] = parent;
                        hierarchy.push_back(r);
                    }
                }
            doc["Entities"] = entities;
            doc["Hierarchy"] = hierarchy;
            YAML::Emitter out;
            out << doc;
            return out.c_str();
        }

        std::string LoadResolvedText(const fs::path& path, int depth)
        {
            if (depth > 16)
            {
                CQ_CORE_ERROR("[Prefab] Variant chain too deep (cycle?): {0}", path.string());
                return {};
            }
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return {};
            std::stringstream ss;
            ss << f.rdbuf();
            std::string text = ss.str();
            if (!IsVariantFile(path))
                return text;
            try
            {
                YAML::Node root = YAML::Load(text);
                return ExpandVariant(root["PrefabVariant"], depth);
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("[Prefab] Variant {0} could not be read: {1}", path.string(), e.what());
                return {};
            }
        }

        // İki tam alt ağaç YAML'ının farkı (eşleşme hiyerarşideki ad yoluyla, UUID'ler tabana çevrilir)
        struct PrefabDiff
        {
            YAML::Node Overrides = YAML::Node(YAML::NodeType::Sequence);
            YAML::Node Added = YAML::Node(YAML::NodeType::Sequence);
            YAML::Node RemovedEntities = YAML::Node(YAML::NodeType::Sequence);
            bool Empty() const { return Overrides.size() == 0 && Added.size() == 0 && RemovedEntities.size() == 0; }
        };

        PrefabDiff DiffDocuments(YAML::Node base, YAML::Node full)
        {
            PrefabDiff diff;
            KeyIndex bk = BuildKeys(base), fk = BuildKeys(full);

            // Eşleşen varlıkların UUID'leri tabandakine çevrilir (referanslar tabanın kimliklerini göstersin)
            std::map<uint64_t, uint64_t> toBase;
            for (const auto& [id, key] : fk.KeyOf)
                if (auto it = bk.IdOf.find(key); it != bk.IdOf.end())
                    toBase[id] = it->second;

            std::map<uint64_t, YAML::Node> baseById, fullById;
            for (YAML::Node e : base["Entities"]) baseById[e["Entity"].as<uint64_t>()] = e;
            for (YAML::Node e : full["Entities"]) fullById[e["Entity"].as<uint64_t>()] = e;

            for (uint64_t id : fk.Order)
            {
                auto mt = toBase.find(id);
                if (mt == toBase.end())
                    continue;
                YAML::Node mine = YAML::Clone(fullById[id]);
                RemapIds(mine, toBase);
                const YAML::Node& theirs = baseById[mt->second];
                YAML::Node comps = YAML::Node(YAML::NodeType::Map);
                YAML::Node removedComps = YAML::Node(YAML::NodeType::Sequence);
                for (auto c = mine.begin(); c != mine.end(); ++c)
                {
                    const std::string name = c->first.as<std::string>();
                    if (name == "Entity")
                        continue;
                    if (!theirs[name] || YAML::Dump(theirs[name]) != YAML::Dump(c->second))
                        comps[name] = c->second;
                }
                for (auto c = theirs.begin(); c != theirs.end(); ++c)
                {
                    const std::string name = c->first.as<std::string>();
                    if (name != "Entity" && !mine[name])
                        removedComps.push_back(name);
                }
                if (comps.size() == 0 && removedComps.size() == 0)
                    continue;
                YAML::Node o;
                o["Key"] = fk.KeyOf[id];
                if (comps.size()) o["Components"] = comps;
                if (removedComps.size()) o["Removed"] = removedComps;
                diff.Overrides.push_back(o);
            }

            for (uint64_t id : fk.Order)
            {
                if (toBase.count(id))
                    continue;
                YAML::Node a;
                YAML::Node ent = YAML::Clone(fullById[id]);
                RemapIds(ent, toBase);
                ent["Entity"] = id;
                a["Entity"] = ent;
                if (auto pt = fk.ParentOf.find(id); pt != fk.ParentOf.end())
                {
                    if (auto bt = toBase.find(pt->second); bt != toBase.end())
                        a["ParentKey"] = bk.KeyOf[bt->second];
                    else
                        a["Parent"] = pt->second;
                }
                diff.Added.push_back(a);
            }

            for (const auto& [key, id] : bk.IdOf)
                if (!fk.IdOf.count(key))
                {
                    // Yalnız en üstteki silinen (ebeveyni duruyorsa)
                    auto pt = bk.ParentOf.find(id);
                    if (pt == bk.ParentOf.end() || fk.IdOf.count(bk.KeyOf[pt->second]))
                        diff.RemovedEntities.push_back(key);
                }
            return diff;
        }

        // Tam alt ağaç YAML'ından (variant'ın istenen hâli) taban farkını üret
        std::string MakeVariantText(const std::string& baseRef, const std::string& fullText)
        {
            const fs::path basePath = ResolveIn(baseRef);
            const PrefabDiff diff = DiffDocuments(YAML::Load(LoadResolvedText(basePath)), YAML::Load(fullText));
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "PrefabVariant" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Base" << YAML::Value << baseRef;
            out << YAML::Key << "Overrides" << YAML::Value << diff.Overrides;
            out << YAML::Key << "Added" << YAML::Value << diff.Added;
            out << YAML::Key << "RemovedEntities" << YAML::Value << diff.RemovedEntities;
            out << YAML::EndMap << YAML::EndMap;
            return std::string(out.c_str()) + "\n";
        }

        // Variant zinciri (kendisi + tabanları) değişim zamanı: taban değişince önbellek geçersiz
        fs::file_time_type ChainTime(const fs::path& path, int depth = 0)
        {
            std::error_code ec;
            auto t = fs::last_write_time(path, ec);
            if (depth < 16 && IsVariantFile(path))
            {
                try
                {
                    YAML::Node root = YAML::LoadFile(path.string());
                    if (root["PrefabVariant"] && root["PrefabVariant"]["Base"])
                        t = std::max(t, ChainTime(ResolveIn(root["PrefabVariant"]["Base"].as<std::string>()), depth + 1));
                }
                catch (...) {}
            }
            return t;
        }

        // Önbellekli şablon: prefab dosyasından yüklenmiş tek köklü sahne
        struct PrefabTemplate
        {
            std::shared_ptr<Scene> TemplateScene;
            entt::entity Root = entt::null;
            fs::file_time_type Time;
        };

        std::unordered_map<std::string, PrefabTemplate>& Templates()
        {
            static std::unordered_map<std::string, PrefabTemplate> s_Templates;
            return s_Templates;
        }

        PrefabTemplate* LoadTemplate(const fs::path& path)
        {
            std::error_code ec;
            if (!fs::exists(path, ec))
            {
                CQ_CORE_ERROR("[Prefab] Not found: {0}", path.string());
                return nullptr;
            }
            auto time = ChainTime(path);
            const std::string key = fs::weakly_canonical(path, ec).string();
            auto& cache = Templates();
            auto it = cache.find(key);
            if (it != cache.end() && it->second.Time == time && it->second.TemplateScene)
                return &it->second;

            PrefabTemplate t;
            t.TemplateScene = std::make_shared<Scene>();
            t.Time = time;
            SceneSerializer serializer(t.TemplateScene);
            std::istringstream text(LoadResolvedText(path));
            if (!serializer.DeserializeFromStream(text))
            {
                CQ_CORE_ERROR("[Prefab] Failed to load {0}", path.string());
                return nullptr;
            }
            t.TemplateScene->m_Registry.view<IDComponent>().each([&](auto e, auto&) {
                if (t.Root == entt::null && !Entity{ e, t.TemplateScene.get() }.GetParent())
                    t.Root = e;
            });
            if (t.Root == entt::null)
            {
                CQ_CORE_ERROR("[Prefab] {0} contains no entities", path.string());
                return nullptr;
            }
            cache[key] = std::move(t);
            return &cache[key];
        }

        void MarkInstance(Entity root, const std::string& prefabPath)
        {
            if (!root.HasComponent<PrefabInstanceComponent>())
                root.AddComponent<PrefabInstanceComponent>();
            root.GetComponent<PrefabInstanceComponent>().Prefab = prefabPath;
        }
    }

    bool PrefabSerializer::IsPrefabPath(const std::string& path)
    {
        return fs::path(path).extension() == Extension;
    }

    fs::path PrefabSerializer::Resolve(const std::string& path)
    {
        fs::path p(SceneSerializer::ResolveAssetPath(path));
        if (p.is_absolute())
            return p;
        std::error_code ec;
        fs::path projectDir = Project::GetActiveProjectDirectory();
        if (!projectDir.empty())
        {
            if (fs::exists(projectDir / p, ec))
                return projectDir / p;
            if (fs::exists(projectDir / "Assets" / p, ec))
                return projectDir / "Assets" / p;
        }
        if (fs::exists(p, ec))
            return fs::absolute(p, ec);
        // Henüz yok (ör. yeni kaydedilecek): proje göreli
        return projectDir.empty() ? fs::absolute(p, ec) : projectDir / p;
    }

    namespace
    {
        fs::path ResolveIn(const std::string& p) { return PrefabSerializer::Resolve(p); }
    }

    bool PrefabSerializer::IsVariant(const std::filesystem::path& path)
    {
        return IsVariantFile(path);
    }

    std::string PrefabSerializer::GetVariantBase(const std::filesystem::path& path)
    {
        if (!IsVariantFile(path))
            return {};
        try
        {
            YAML::Node root = YAML::LoadFile(path.string());
            return root["PrefabVariant"]["Base"].as<std::string>();
        }
        catch (...) { return {}; }
    }

    bool PrefabSerializer::CreateVariant(const std::filesystem::path& basePath, const std::filesystem::path& variantPath)
    {
        std::error_code ec;
        if (!fs::exists(basePath, ec))
            return false;
        std::ofstream f(variantPath, std::ios::binary);
        if (!f)
            return false;
        f << "PrefabVariant:\n  Base: " << ToProjectPath(basePath) << "\n  Overrides: []\n  Added: []\n  RemovedEntities: []\n";
        CQ_CORE_INFO("[Prefab] Created variant {0} of {1}", variantPath.filename().string(), basePath.filename().string());
        return true;
    }

    std::string PrefabSerializer::ToProjectPath(const fs::path& path)
    {
        std::error_code ec;
        fs::path abs = fs::weakly_canonical(fs::absolute(path, ec), ec);
        std::string portable = SceneSerializer::ToAssetPath(abs.string());
        std::replace(portable.begin(), portable.end(), '\\', '/');
        return portable;
    }

    void PrefabSerializer::ClearCache()
    {
        Templates().clear();
    }

    namespace
    {
        struct OverrideCheck
        {
            uint64_t Entity = 0;
            bool Value = false;
            std::chrono::steady_clock::time_point Next{};
        } s_OverrideCheck;
    }

    bool PrefabSerializer::Serialize(Entity root, const fs::path& filepath)
    {
        if (!root)
            return false;
        // Geçici sahneye kopyala: yalnızca alt ağaç, kök ebeveynsiz; kökün dünya dönüşümü yerel olarak saklanır
        auto temp = std::make_shared<Scene>();
        std::unordered_map<uint64_t, uint64_t> uuidMap;
        Entity copy = temp->DuplicateEntityTree(root, "", Entity{}, &uuidMap);
        temp->RemapEntityReferences(copy, uuidMap);
        if (root.GetParent())
        {
            auto& tc = copy.GetComponent<TransformComponent>();
            Math::DecomposeTransform(root.GetComponent<TransformComponent>().WorldTransform, tc.Position, tc.Rotation, tc.Scale);
        }
        // Şablon kökü kendine referans vermez (iç içe prefab'lar çocuklarda işaretli kalır)
        if (copy.HasComponent<PrefabInstanceComponent>())
            copy.RemoveComponent<PrefabInstanceComponent>();

        std::error_code ec;
        if (filepath.has_parent_path())
            fs::create_directories(filepath.parent_path(), ec);
        const std::string variantBase = GetVariantBase(filepath);
        std::ofstream out(filepath, std::ios::binary);
        if (!out.is_open())
        {
            CQ_CORE_ERROR("[Prefab] Cannot write {0}", filepath.string());
            return false;
        }
        SceneSerializer serializer(temp);
        if (!variantBase.empty())
        {
            // Variant: yalnız tabandan farklar yazılır
            std::stringstream full;
            serializer.SerializeToStream(full);
            out << MakeVariantText(variantBase, full.str());
        }
        else
            serializer.SerializeToStream(out);
        out.close();
        // Aynı yoldaki eski şablon geçersiz
        Templates().erase(fs::weakly_canonical(filepath, ec).string());
        CQ_CORE_INFO("[Prefab] Saved {0}", filepath.filename().string());
        return true;
    }

    Entity PrefabSerializer::Instantiate(Scene& scene, const std::string& path, Entity parent)
    {
        fs::path resolved = Resolve(path);
        PrefabTemplate* t = LoadTemplate(resolved);
        if (!t)
            return {};
        Entity source{ t->Root, t->TemplateScene.get() };
        std::unordered_map<uint64_t, uint64_t> uuidMap;
        Entity root = scene.DuplicateEntityTree(source, "", parent, &uuidMap);
        scene.RemapEntityReferences(root, uuidMap);
        MarkInstance(root, ToProjectPath(resolved));
        scene.UpdateWorldTransforms();
        scene.OnEntityTreeSpawned(root);
        return root;
    }

    Entity PrefabSerializer::Deserialize(const std::shared_ptr<Scene>& scene, const fs::path& filepath, Entity parent)
    {
        return scene ? Instantiate(*scene, filepath.string(), parent) : Entity{};
    }

    Entity PrefabSerializer::FindInstanceRoot(Entity entity)
    {
        for (Entity e = entity; e; e = e.GetParent())
            if (e.HasComponent<PrefabInstanceComponent>())
                return e;
        return {};
    }

    std::string PrefabSerializer::GetPrefabPath(Entity entity)
    {
        Entity root = FindInstanceRoot(entity);
        return root ? root.GetComponent<PrefabInstanceComponent>().Prefab : std::string();
    }

    bool PrefabSerializer::HasOverrides(Entity instanceRoot)
    {
        Entity root = FindInstanceRoot(instanceRoot);
        if (!root)
            return false;
        // Inspector her kare sorar: sonuç kısa süre saklanır (bekleme, ölçümün maliyetiyle orantılı)
        const auto now = std::chrono::steady_clock::now();
        const uint64_t id = root.GetComponent<IDComponent>().ID;
        if (s_OverrideCheck.Entity == id && now < s_OverrideCheck.Next)
            return s_OverrideCheck.Value;

        const bool value = [&]() {
            const fs::path path = Resolve(root.GetComponent<PrefabInstanceComponent>().Prefab);
            std::error_code ec;
            if (!fs::exists(path, ec))
                return true;
            PrefabTemplate* t = LoadTemplate(path);
            if (!t)
                return false;
            // Iki taraf da şu anki biçimde yazılır: eski dosyadaki eksik alanlar değişiklik sayılmasın
            std::stringstream baseText, fullText;
            SceneSerializer(t->TemplateScene).SerializeToStream(baseText);
            auto temp = std::make_shared<Scene>();
            std::unordered_map<uint64_t, uint64_t> uuidMap;
            Entity copy = temp->DuplicateEntityTree(root, "", Entity{}, &uuidMap);
            temp->RemapEntityReferences(copy, uuidMap);
            if (copy.HasComponent<PrefabInstanceComponent>())
                copy.RemoveComponent<PrefabInstanceComponent>();
            SceneSerializer(temp).SerializeToStream(fullText);
            YAML::Node base = YAML::Load(baseText.str()), full = YAML::Load(fullText.str());
            // Kökün yeri, dönüşü ve adı örneğe aittir (Unity bunları override saymaz)
            auto normalizeRoot = [](YAML::Node doc) {
                std::set<uint64_t> children;
                if (doc["Hierarchy"])
                    for (const auto& r : doc["Hierarchy"])
                        children.insert(r["Child"].as<uint64_t>());
                for (YAML::Node e : doc["Entities"])
                    if (!children.count(e["Entity"].as<uint64_t>()))
                    {
                        if (e["TransformComponent"])
                        {
                            e["TransformComponent"].remove("Translation");
                            e["TransformComponent"].remove("Position");
                            e["TransformComponent"].remove("Rotation");
                        }
                        if (e["TagComponent"])
                            e["TagComponent"].remove("Tag");
                        break;
                    }
            };
            normalizeRoot(base);
            normalizeRoot(full);
            return !DiffDocuments(base, full).Empty();
        }();
        const auto cost = std::chrono::steady_clock::now() - now;
        s_OverrideCheck.Entity = id;
        s_OverrideCheck.Value = value;
        s_OverrideCheck.Next = std::chrono::steady_clock::now() + std::max<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250), cost * 20);
        return value;
    }

    bool PrefabSerializer::ApplyInstance(Entity instanceRoot)
    {
        Entity root = FindInstanceRoot(instanceRoot);
        if (!root)
            return false;
        s_OverrideCheck.Entity = 0;
        const std::string path = root.GetComponent<PrefabInstanceComponent>().Prefab;
        return Serialize(root, Resolve(path));
    }

    Entity PrefabSerializer::RevertInstance(Entity instanceRoot)
    {
        Entity root = FindInstanceRoot(instanceRoot);
        if (!root)
            return {};
        s_OverrideCheck.Entity = 0;
        Scene* scene = root.GetScene();
        const std::string path = root.GetComponent<PrefabInstanceComponent>().Prefab;
        const TransformComponent transform = root.GetComponent<TransformComponent>();
        const std::string name = root.GetComponent<TagComponent>().Tag;
        Entity parent = root.GetParent();
        Entity fresh = Instantiate(*scene, path, parent);
        if (!fresh)
            return {};
        // Örnek düzeyindeki yerleşim korunur: konum, dönüş, ölçek, isim
        auto& tc = fresh.GetComponent<TransformComponent>();
        tc.Position = transform.Position;
        tc.Rotation = transform.Rotation;
        tc.Scale = transform.Scale;
        fresh.GetComponent<TagComponent>().Tag = name;
        scene->DestroyEntity(root);
        scene->UpdateWorldTransforms();
        return fresh;
    }
}
