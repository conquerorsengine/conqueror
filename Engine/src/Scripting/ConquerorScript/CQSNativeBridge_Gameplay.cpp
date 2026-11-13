#include "CQSNativeBridge.h"
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/GameplayTags.h"
#include "Scene/AbilitySystem.h"
#include "Scene/GameFramework.h"
#include "Scene/LevelStreaming.h"
#include "Scene/AutoLOD.h"
#include "Scene/AudioOcclusion.h"
#include "Scene/VertexPaint.h"
#include "AISystem/EQS/EnvQuery.h"
#include "Scene/Fracture.h"
#include "Scene/Ragdoll.h"
#include "Scene/Signals.h"
#include "Scene/Tweens.h"
#include "Scene/MultiMesh.h"
#include "Scene/GodotNodes.h"
#include "Scene/GameWindow.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Core/Data/DataTable.h"
#include "Core/Animation/AnimationStateMachine.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <unordered_map>
#include <limits>
#include <string>
#include <vector>

// Unreal oyun çerçevesi parçaları: Gameplay Tags, Data Tables, Gameplay Ability System.
// Hedef argümanı: varlık kimliği (sayı), varlık adı (metin) ya da -1/null (betiğin kendi varlığı).
namespace Conqueror::CQS
{
    namespace
    {
        Scene* ActiveScene() { return ScriptContext::GetActiveScene(); }

        Entity EntityFromId(int64_t id)
        {
            Scene* scene = ActiveScene();
            if (!scene || id < 0 || id >= (int64_t)ScriptContext::NullEntity)
                return {};
            entt::entity h = (entt::entity)(uint32_t)id;
            return scene->m_Registry.valid(h) ? Entity{ h, scene } : Entity{};
        }

        Entity Current()
        {
            uint32_t id = ScriptContext::GetCurrentEntity();
            return id == ScriptContext::NullEntity ? Entity{} : EntityFromId(id);
        }

        Entity Target(const Value& v)
        {
            if (v.IsNumber())
            {
                const double id = v.ToNumber();
                return id < 0 ? Current() : EntityFromId((int64_t)id);
            }
            if (v.IsString())
            {
                Scene* scene = ActiveScene();
                if (!scene) return {};
                auto view = scene->m_Registry.view<TagComponent>();
                for (auto e : view)
                    if (view.get<TagComponent>(e).Tag == v.AsString()->Value)
                        return Entity{ e, scene };
                return {};
            }
            if (v.IsNull())
                return Current();
            return {};
        }

        std::string Text(const Value& v) { return v.IsString() ? v.AsString()->Value : v.IsNull() ? std::string() : v.ToString(); }
        float Num(int argc, Value* args, int i, float def) { return i < argc && args[i].IsNumber() ? (float)args[i].ToNumber() : def; }
        bool Truthy(const Value& v) { return v.IsBool() ? v.AsBool() : v.IsNumber() ? v.ToNumber() != 0.0 : !v.IsNull(); }

        // Liste ya da virgüllü metin -> etiketler
        std::vector<std::string> TagsOf(const Value& v)
        {
            std::vector<std::string> out;
            if (v.IsList())
            {
                for (auto& e : v.AsList()->Elements)
                    if (e.IsString()) out.push_back(GameplayTags::Normalize(e.AsString()->Value));
                return out;
            }
            if (v.IsString())
                return AbilitySystem::ParseTagList(v.AsString()->Value);
            return out;
        }

        Value StringList(const std::vector<std::string>& items)
        {
            auto* l = new CQSListObject();
            for (auto& s : items) l->Elements.push_back(Value::MakeString(s));
            return Value::MakeObject(l);
        }

        Value CellValue(const DataTableCell& c)
        {
            switch (c.Type)
            {
                case DataTableCell::Kind::Number:
                    return std::floor(c.Number) == c.Number && std::fabs(c.Number) < 1e15 ? Value::MakeInt((int64_t)c.Number) : Value::MakeFloat(c.Number);
                case DataTableCell::Kind::Bool: return Value::MakeBool(c.Bool);
                case DataTableCell::Kind::Empty: return Value::MakeNull();
                default: return Value::MakeString(c.Text);
            }
        }

        Value RowValue(const DataTable& t, const std::vector<DataTableCell>& row)
        {
            auto* m = new CQSMapObject();
            for (size_t c = 0; c < t.Columns.size() && c < row.size(); ++c)
                m->Entries[t.Columns[c]] = CellValue(row[c]);
            return Value::MakeObject(m);
        }

        std::string CellText(const DataTable& t, const std::vector<DataTableCell>& row, const char* column)
        {
            int i = t.ColumnIndex(column);
            return i >= 0 && i < (int)row.size() ? row[(size_t)i].Text : std::string();
        }

        Value MapGet(const Value& map, const char* key)
        {
            if (!map.IsMap()) return Value::MakeNull();
            auto& e = map.AsMap()->Entries;
            auto it = e.find(key);
            return it != e.end() ? it->second : Value::MakeNull();
        }

        EffectStacking ParseStacking(const std::string& s)
        {
            std::string l = s;
            std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (l == "none" || l == "separate") return EffectStacking::None;
            if (l == "stack" || l == "stacks") return EffectStacking::Stack;
            return EffectStacking::Refresh;
        }

        // {name, duration, period, executeOnApply, modifiers, tags, requiredTags, blockedTags, stacking, maxStacks}
        GameplayEffectSpec SpecFromMap(const Value& m)
        {
            GameplayEffectSpec s;
            s.Name = Text(MapGet(m, "name"));
            if (auto v = MapGet(m, "duration"); v.IsNumber()) s.Duration = (float)v.ToNumber();
            if (auto v = MapGet(m, "period"); v.IsNumber()) s.Period = (float)v.ToNumber();
            if (auto v = MapGet(m, "executeOnApply"); !v.IsNull()) s.ExecuteOnApply = Truthy(v);
            Value mods = MapGet(m, "modifiers");
            if (mods.IsString()) AbilitySystem::ParseModifiers(mods.AsString()->Value, s.Modifiers);
            else if (mods.IsList())
                for (auto& item : mods.AsList()->Elements)
                {
                    AttributeModifier mod;
                    mod.Attribute = Text(MapGet(item, "attribute"));
                    std::string op = Text(MapGet(item, "op"));
                    mod.Op = op == "mul" || op == "multiply" ? ModifierOp::Multiply : op == "override" || op == "set" ? ModifierOp::Override : ModifierOp::Add;
                    mod.Value = (float)MapGet(item, "value").ToNumber();
                    if (!mod.Attribute.empty()) s.Modifiers.push_back(mod);
                }
            s.GrantedTags = TagsOf(MapGet(m, "tags"));
            s.RequiredTags = TagsOf(MapGet(m, "requiredTags"));
            s.BlockedTags = TagsOf(MapGet(m, "blockedTags"));
            if (auto v = MapGet(m, "stacking"); v.IsString()) s.Stacking = ParseStacking(v.AsString()->Value);
            if (auto v = MapGet(m, "maxStacks"); v.IsNumber()) s.MaxStacks = std::max(1, (int)v.ToNumber());
            return s;
        }

        bool SpecFromTable(const std::string& path, const std::string& key, GameplayEffectSpec& s)
        {
            const DataTable& t = DataTables::Load(path);
            const auto* row = t.FindRow(key);
            if (!row) return false;
            s.Name = (*row)[0].Text;
            auto num = [&](const char* col, float def) {
                int i = t.ColumnIndex(col);
                return i >= 0 && (*row)[(size_t)i].Type == DataTableCell::Kind::Number ? (float)(*row)[(size_t)i].Number : def;
            };
            s.Duration = num("Duration", 0.0f);
            s.Period = num("Period", 0.0f);
            s.MaxStacks = std::max(1, (int)num("MaxStacks", 1.0f));
            AbilitySystem::ParseModifiers(CellText(t, *row, "Modifiers"), s.Modifiers);
            s.GrantedTags = AbilitySystem::ParseTagList(CellText(t, *row, "Tags"));
            s.RequiredTags = AbilitySystem::ParseTagList(CellText(t, *row, "RequiredTags"));
            s.BlockedTags = AbilitySystem::ParseTagList(CellText(t, *row, "BlockedTags"));
            std::string stacking = CellText(t, *row, "Stacking");
            if (!stacking.empty()) s.Stacking = ParseStacking(stacking);
            return true;
        }
    }

    namespace
    {
        // Animasyon hedefi: varlığın kendisi ya da altındaki ilk AnimationComponent (karakter kökü verilebilir)
        Entity AnimTarget(const Value& v)
        {
            Entity root = Target(v);
            if (!root)
                return {};
            std::deque<Entity> queue{ root };
            for (int guard = 0; !queue.empty() && guard < 512; ++guard)
            {
                Entity e = queue.front();
                queue.pop_front();
                if (e.HasComponent<AnimationComponent>())
                    return e;
                for (Entity c : e.GetChildren())
                    queue.push_back(c);
            }
            return {};
        }

        const Model* ModelOf(Entity e)
        {
            return e && e.HasComponent<MeshRendererComponent>() ? e.GetComponent<MeshRendererComponent>().ModelData.get() : nullptr;
        }

        // .cqmontage dosyaları değişiklik zamanıyla önbelleklenir; dosya değilse modeldeki klip adıyla geçici montaj
        std::shared_ptr<AnimMontage> MontageFor(const std::string& nameOrPath, const Model* model)
        {
            namespace fs = std::filesystem;
            if (nameOrPath.size() > 10 && nameOrPath.ends_with(".cqmontage"))
            {
                struct Entry { fs::file_time_type Time; std::shared_ptr<AnimMontage> Montage; };
                static std::unordered_map<std::string, Entry> cache;
                const fs::path file = PrefabSerializer::Resolve(nameOrPath);
                std::error_code ec;
                const auto time = fs::last_write_time(file, ec);
                if (ec)
                    return nullptr;
                auto& entry = cache[file.string()];
                if (!entry.Montage || entry.Time != time)
                {
                    entry.Montage = AnimMontage::Load(file.string());
                    entry.Time = time;
                }
                return entry.Montage;
            }
            if (!model)
                return nullptr;
            for (size_t i = 0; i < model->Animations.size(); ++i)
                if (model->Animations[i] && model->Animations[i]->Name == nameOrPath)
                {
                    auto m = std::make_shared<AnimMontage>();
                    m->Name = nameOrPath;
                    m->ClipName = nameOrPath;
                    m->ClipIndex = (int)i;
                    return m;
                }
            return nullptr;
        }
    }

    void RegisterGameplayNatives(Scripting::NativeRegistry* vm)
    {
        // ================================================================ Gameplay Tags
        vm->DefineNative("TagAdd", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc >= 2 && GameplayTags::Add(Target(args[0]), Text(args[1])));
        }, 2);
        vm->DefineNative("TagRemove", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc >= 2 && GameplayTags::Remove(Target(args[0]), Text(args[1]), argc > 2 && Truthy(args[2])));
        }, -1);
        vm->DefineNative("TagHas", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc >= 2 && GameplayTags::Has(Target(args[0]), Text(args[1]), argc > 2 && Truthy(args[2])));
        }, -1);
        vm->DefineNative("TagHasAny", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc >= 2 && GameplayTags::HasAny(Target(args[0]), TagsOf(args[1]), argc > 2 && Truthy(args[2])));
        }, -1);
        vm->DefineNative("TagHasAll", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc >= 2 && GameplayTags::HasAll(Target(args[0]), TagsOf(args[1]), argc > 2 && Truthy(args[2])));
        }, -1);
        vm->DefineNative("TagGetAll", [](int argc, Value* args) -> Value {
            return StringList(GameplayTags::GetAll(argc > 0 ? Target(args[0]) : Current()));
        }, -1);
        vm->DefineNative("FindEntitiesWithGameplayTag", [](int argc, Value* args) -> Value {
            auto* l = new CQSListObject();
            if (Scene* scene = ActiveScene(); scene && argc > 0)
                for (uint32_t id : GameplayTags::FindEntities(*scene, Text(args[0]), argc > 1 && Truthy(args[1])))
                    l->Elements.push_back(Value::MakeInt((int64_t)id));
            return Value::MakeObject(l);
        }, -1);

        // ================================================================ Data Tables
        vm->DefineNative("DataTableGetRow", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeNull();
            const DataTable& t = DataTables::Load(Text(args[0]));
            const auto* row = t.FindRow(Text(args[1]));
            return row ? RowValue(t, *row) : Value::MakeNull();
        }, 2);
        vm->DefineNative("DataTableGetValue", [](int argc, Value* args) -> Value {
            if (argc < 3) return Value::MakeNull();
            const DataTable& t = DataTables::Load(Text(args[0]));
            const auto* row = t.FindRow(Text(args[1]));
            int c = t.ColumnIndex(Text(args[2]));
            return row && c >= 0 && c < (int)row->size() ? CellValue((*row)[(size_t)c]) : Value::MakeNull();
        }, 3);
        vm->DefineNative("DataTableRowNames", [](int argc, Value* args) -> Value {
            std::vector<std::string> names;
            if (argc > 0)
                for (auto& row : DataTables::Load(Text(args[0])).Rows)
                    if (!row.empty()) names.push_back(row[0].Text);
            return StringList(names);
        }, 1);
        vm->DefineNative("DataTableRows", [](int argc, Value* args) -> Value {
            auto* l = new CQSListObject();
            if (argc > 0)
            {
                const DataTable& t = DataTables::Load(Text(args[0]));
                for (auto& row : t.Rows) l->Elements.push_back(RowValue(t, row));
            }
            return Value::MakeObject(l);
        }, 1);
        vm->DefineNative("DataTableColumns", [](int argc, Value* args) -> Value {
            return StringList(argc > 0 ? DataTables::Load(Text(args[0])).Columns : std::vector<std::string>{});
        }, 1);
        vm->DefineNative("DataTableFindRows", [](int argc, Value* args) -> Value {
            // DataTableFindRows(path, column, value): hücre metni (büyük/küçük harf duyarsız) ya da sayı eşit olan satırlar
            auto* l = new CQSListObject();
            if (argc < 3) return Value::MakeObject(l);
            const DataTable& t = DataTables::Load(Text(args[0]));
            int c = t.ColumnIndex(Text(args[1]));
            if (c < 0) return Value::MakeObject(l);
            std::string want = Text(args[2]);
            std::transform(want.begin(), want.end(), want.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
            for (auto& row : t.Rows)
            {
                const DataTableCell& cell = row[(size_t)c];
                bool match = args[2].IsNumber() ? cell.Type == DataTableCell::Kind::Number && cell.Number == args[2].ToNumber() : false;
                if (!match)
                {
                    std::string have = cell.Text;
                    std::transform(have.begin(), have.end(), have.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                    match = have == want;
                }
                if (match) l->Elements.push_back(RowValue(t, row));
            }
            return Value::MakeObject(l);
        }, 3);
        vm->DefineNative("DataTableError", [](int argc, Value* args) -> Value {
            return Value::MakeString(argc > 0 ? DataTables::Load(Text(args[0])).Error : std::string());
        }, 1);

        // ================================================================ Game Mode / Player Start
        vm->DefineNative("GameModeRestartPlayer", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene) return Value::MakeNull();
            uint32_t pawn = GameFramework::RestartPlayer(*scene, argc > 0 ? Text(args[0]) : std::string());
            return pawn != 0xFFFFFFFFu ? Value::MakeInt((int64_t)pawn) : Value::MakeNull();
        }, -1);
        vm->DefineNative("GameModeRespawn", [](int argc, Value* args) -> Value {
            if (Scene* scene = ActiveScene()) GameFramework::ScheduleRespawn(*scene, Num(argc, args, 0, -1.0f));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("GameModeGetPawn", [](int, Value*) -> Value {
            Scene* scene = ActiveScene();
            uint32_t pawn = scene ? GameFramework::GetPawn(*scene) : 0xFFFFFFFFu;
            return pawn != 0xFFFFFFFFu ? Value::MakeInt((int64_t)pawn) : Value::MakeNull();
        }, 0);
        vm->DefineNative("GameModeFindPlayerStart", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            uint32_t s = scene ? GameFramework::FindPlayerStart(*scene, argc > 0 ? Text(args[0]) : std::string()) : 0xFFFFFFFFu;
            return s != 0xFFFFFFFFu ? Value::MakeInt((int64_t)s) : Value::MakeNull();
        }, -1);

        // ================================================================ Level streaming (ek sahne yükleme)
        vm->DefineNative("LevelLoadAdditive", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1) return Value::MakeNull();
            uint32_t root = LevelStreaming::LoadAdditive(*scene, Text(args[0]), { Num(argc, args, 1, 0.0f), Num(argc, args, 2, 0.0f), Num(argc, args, 3, 0.0f) });
            return root != 0xFFFFFFFFu ? Value::MakeInt((int64_t)root) : Value::MakeNull();
        }, -1);
        vm->DefineNative("LevelUnload", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            return Value::MakeBool(scene && argc > 0 && LevelStreaming::Unload(*scene, Text(args[0])));
        }, 1);
        vm->DefineNative("LevelIsLoaded", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            return Value::MakeBool(scene && argc > 0 && LevelStreaming::IsLoaded(*scene, Text(args[0])));
        }, 1);
        vm->DefineNative("LevelGetLoaded", [](int, Value*) -> Value {
            Scene* scene = ActiveScene();
            return StringList(scene ? LevelStreaming::GetLoaded(*scene) : std::vector<std::string>{});
        }, 0);

        // ================================================================ Ability System (nitelikler / efektler / yetenekler)
        vm->DefineNative("AttrGet", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeNull();
            GameplayAttribute* a = AbilitySystem::FindAttribute(Target(args[0]), Text(args[1]));
            return a ? Value::MakeFloat(a->Current) : Value::MakeNull();
        }, 2);
        vm->DefineNative("AttrGetBase", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeNull();
            GameplayAttribute* a = AbilitySystem::FindAttribute(Target(args[0]), Text(args[1]));
            return a ? Value::MakeFloat(a->Base) : Value::MakeNull();
        }, 2);
        vm->DefineNative("AttrSetBase", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc >= 3 && AbilitySystem::SetAttributeBase(Target(args[0]), Text(args[1]), Num(argc, args, 2, 0.0f)));
        }, 3);
        vm->DefineNative("AttrAdd", [](int argc, Value* args) -> Value {
            // Anlık efekt: tabana ekle (sınırlara kırpılır)
            if (argc < 3) return Value::MakeBool(false);
            GameplayEffectSpec s;
            s.Name = "AttrAdd";
            s.Modifiers.push_back({ Text(args[1]), ModifierOp::Add, Num(argc, args, 2, 0.0f) });
            return Value::MakeBool(AbilitySystem::ApplyEffect(Target(args[0]), s) != 0);
        }, 3);
        vm->DefineNative("AttrDefine", [](int argc, Value* args) -> Value {
            const float inf = std::numeric_limits<float>::infinity();
            return Value::MakeBool(argc >= 3 && AbilitySystem::DefineAttribute(Target(args[0]), Text(args[1]), Num(argc, args, 2, 0.0f), Num(argc, args, 3, -inf), Num(argc, args, 4, inf)));
        }, -1);
        vm->DefineNative("EffectApply", [](int argc, Value* args) -> Value {
            if (argc < 2 || !args[1].IsMap()) return Value::MakeInt(0);
            uint32_t src = ScriptContext::GetCurrentEntity();
            return Value::MakeInt((int64_t)AbilitySystem::ApplyEffect(Target(args[0]), SpecFromMap(args[1]), src));
        }, 2);
        vm->DefineNative("EffectApplyFromTable", [](int argc, Value* args) -> Value {
            // EffectApplyFromTable(target, "Assets/Data/Effects.csv", "Burning"): sütunlar Duration, Period, Modifiers, Tags, Stacking, MaxStacks
            if (argc < 3) return Value::MakeInt(0);
            GameplayEffectSpec s;
            if (!SpecFromTable(Text(args[1]), Text(args[2]), s)) return Value::MakeInt(0);
            return Value::MakeInt((int64_t)AbilitySystem::ApplyEffect(Target(args[0]), s, ScriptContext::GetCurrentEntity()));
        }, 3);
        vm->DefineNative("EffectRemove", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeInt(0);
            Entity e = Target(args[0]);
            if (args[1].IsNumber()) return Value::MakeInt(AbilitySystem::RemoveEffect(e, (uint32_t)args[1].ToNumber()));
            return Value::MakeInt(AbilitySystem::RemoveEffectsByName(e, Text(args[1])));
        }, 2);
        vm->DefineNative("EffectStacks", [](int argc, Value* args) -> Value {
            return Value::MakeInt(argc >= 2 ? AbilitySystem::GetEffectStacks(Target(args[0]), Text(args[1])) : 0);
        }, 2);
        vm->DefineNative("EffectHas", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc >= 2 && AbilitySystem::GetEffectStacks(Target(args[0]), Text(args[1])) > 0);
        }, 2);
        vm->DefineNative("AbilityGrant", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            GameplayAbilitySpec s;
            s.Name = Text(args[1]);
            s.Cooldown = Num(argc, args, 2, 0.0f);
            if (argc > 3) s.CostAttribute = Text(args[3]);
            s.CostAmount = Num(argc, args, 4, 0.0f);
            if (argc > 5) s.BlockedTags = TagsOf(args[5]);
            if (argc > 6) s.RequiredTags = TagsOf(args[6]);
            return Value::MakeBool(AbilitySystem::GrantAbility(Target(args[0]), s));
        }, -1);
        vm->DefineNative("AbilityTryActivate", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc >= 2 && AbilitySystem::TryActivateAbility(Target(args[0]), Text(args[1])));
        }, 2);
        vm->DefineNative("AbilityCooldown", [](int argc, Value* args) -> Value {
            return Value::MakeFloat(argc >= 2 ? AbilitySystem::GetCooldownRemaining(Target(args[0]), Text(args[1])) : 0.0f);
        }, 2);
        vm->DefineNative("AbilityLastFailure", [](int argc, Value* args) -> Value {
            AbilitySystemComponent* asc = AbilitySystem::Get(argc > 0 ? Target(args[0]) : Current());
            return Value::MakeString(asc ? asc->LastFailure : std::string("no Ability System"));
        }, -1);

        // ================================================================ Anim Montage / Notify
        vm->DefineNative("AnimMontagePlay", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeFloat(0.0);
            Entity e = AnimTarget(args[0]);
            if (!e) return Value::MakeFloat(0.0);
            const Model* model = ModelOf(e);
            auto montage = MontageFor(Text(args[1]), model);
            if (!montage) return Value::MakeFloat(0.0);
            // Bir kez oynatmaya özel ayarlar dosyadaki montajı değiştirmesin
            const float blendIn = Num(argc, args, 4, -1.0f), blendOut = Num(argc, args, 5, -1.0f);
            const std::string fromBone = argc > 6 ? Text(args[6]) : std::string();
            if (blendIn >= 0.0f || blendOut >= 0.0f || !fromBone.empty())
            {
                montage = std::make_shared<AnimMontage>(*montage);
                if (blendIn >= 0.0f) montage->BlendIn = blendIn;
                if (blendOut >= 0.0f) montage->BlendOut = blendOut;
                if (!fromBone.empty()) montage->FromBone = fromBone;
            }
            return Value::MakeFloat(AnimationStateMachine::PlayMontage(e.GetComponent<AnimationComponent>(), model, montage,
                                                                       Num(argc, args, 2, 1.0f), argc > 3 ? Text(args[3]) : std::string()));
        }, -1);
        vm->DefineNative("AnimMontageStop", [](int argc, Value* args) -> Value {
            Entity e = AnimTarget(argc > 0 ? args[0] : Value::MakeNull());
            if (!e || !e.GetComponent<AnimationComponent>().Montage.Active) return Value::MakeBool(false);
            AnimationStateMachine::StopMontage(e.GetComponent<AnimationComponent>(), Num(argc, args, 1, -1.0f));
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("AnimMontageIsPlaying", [](int argc, Value* args) -> Value {
            Entity e = AnimTarget(argc > 0 ? args[0] : Value::MakeNull());
            if (!e) return Value::MakeBool(false);
            const auto& mi = e.GetComponent<AnimationComponent>().Montage;
            if (!mi.Active || !mi.Montage) return Value::MakeBool(false);
            return Value::MakeBool(argc < 2 || args[1].IsNull() || mi.Montage->Name == Text(args[1]));
        }, -1);
        vm->DefineNative("AnimMontageGetName", [](int argc, Value* args) -> Value {
            Entity e = AnimTarget(argc > 0 ? args[0] : Value::MakeNull());
            if (!e) return Value::MakeString("");
            const auto& mi = e.GetComponent<AnimationComponent>().Montage;
            return Value::MakeString(mi.Active && mi.Montage ? mi.Montage->Name : std::string());
        }, -1);
        vm->DefineNative("AnimMontageJumpToSection", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            Entity e = AnimTarget(args[0]);
            return Value::MakeBool(e && AnimationStateMachine::JumpToSection(e.GetComponent<AnimationComponent>(), ModelOf(e), Text(args[1])));
        }, 2);
        vm->DefineNative("AnimMontageSetNextSection", [](int argc, Value* args) -> Value {
            if (argc < 3) return Value::MakeBool(false);
            Entity e = AnimTarget(args[0]);
            return Value::MakeBool(e && AnimationStateMachine::SetNextSection(e.GetComponent<AnimationComponent>(), Text(args[1]), Text(args[2])));
        }, 3);
        vm->DefineNative("AnimMontageGetSection", [](int argc, Value* args) -> Value {
            Entity e = AnimTarget(argc > 0 ? args[0] : Value::MakeNull());
            if (!e) return Value::MakeString("");
            const auto& mi = e.GetComponent<AnimationComponent>().Montage;
            return Value::MakeString(mi.Active ? mi.Section : std::string());
        }, -1);
        vm->DefineNative("AnimMontageGetPosition", [](int argc, Value* args) -> Value {
            Entity e = AnimTarget(argc > 0 ? args[0] : Value::MakeNull());
            if (!e) return Value::MakeFloat(0.0);
            const auto& mi = e.GetComponent<AnimationComponent>().Montage;
            return Value::MakeFloat(mi.Active ? mi.Time : 0.0f);
        }, -1);
        vm->DefineNative("AnimMontageGetWeight", [](int argc, Value* args) -> Value {
            Entity e = AnimTarget(argc > 0 ? args[0] : Value::MakeNull());
            if (!e) return Value::MakeFloat(0.0);
            const auto& mi = e.GetComponent<AnimationComponent>().Montage;
            return Value::MakeFloat(mi.Active ? std::clamp(mi.Weight, 0.0f, 1.0f) : 0.0f);
        }, -1);
        vm->DefineNative("AnimGetState", [](int argc, Value* args) -> Value {
            Entity e = AnimTarget(argc > 0 ? args[0] : Value::MakeNull());
            if (!e) return Value::MakeString("");
            const auto& ac = e.GetComponent<AnimationComponent>();
            const int layer = (int)Num(argc, args, 1, 0.0f);
            if (layer < 0 || layer >= (int)ac.LayerRuntime.size()) return Value::MakeString("");
            return Value::MakeString(ac.LayerRuntime[(size_t)layer].CurrentState);
        }, -1);
        // ================================================================ Model atama
        vm->DefineNative("EntitySetModel", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            Entity e = Target(args[0]);
            if (!e) return Value::MakeBool(false);
            const std::string path = Text(args[1]);
            const std::filesystem::path file = PrefabSerializer::Resolve(path);
            auto model = ModelLoader::Load(file.string());
            if (!model) return Value::MakeBool(false);
            if (!e.HasComponent<MeshRendererComponent>()) e.AddComponent<MeshRendererComponent>();
            auto& mc = e.GetComponent<MeshRendererComponent>();
            mc.ModelPath = PrefabSerializer::ToProjectPath(file);
            mc.ModelData = model;
            mc.MaterialInstance = nullptr;
            mc.MaterialPath.clear();
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("EntityGetModel", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeString(e && e.HasComponent<MeshRendererComponent>() ? e.GetComponent<MeshRendererComponent>().ModelPath : std::string());
        }, -1);

        // ================================================================ Ses örtme / eşzamanlılık
        vm->DefineNative("AudioGetOcclusion", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeFloat(e && e.HasComponent<SoundOcclusionComponent>() ? e.GetComponent<SoundOcclusionComponent>().Occlusion : 0.0f);
        }, -1);
        vm->DefineNative("AudioIsOccluded", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(e && e.HasComponent<SoundOcclusionComponent>() && e.GetComponent<SoundOcclusionComponent>().Occluded);
        }, -1);

        // ================================================================ EQS (Environment Query System)
        // Bağlam değeri: "querier" | varlık (kimlik / ad) | {x,y,z}
        auto contextOf = [](const Value& v) -> EnvContext {
            EnvContext c;
            if (v.IsNull() || (v.IsString() && v.AsString()->Value == "querier"))
                return c;
            if (v.IsMap())
            {
                c.Type = EnvContext::Kind::Point;
                c.Point = glm::vec3((float)MapGet(v, "x").ToNumber(), (float)MapGet(v, "y").ToNumber(), (float)MapGet(v, "z").ToNumber());
                return c;
            }
            if (Entity e = Target(v))
            {
                c.Type = EnvContext::Kind::Entity;
                c.EntityId = (uint32_t)(entt::entity)e;
            }
            return c;
        };
        auto itemsToValue = [](const std::vector<EnvQueryItem>& items) -> Value {
            auto* list = new CQSListObject();
            for (const auto& it : items)
            {
                auto* m = new CQSMapObject();
                m->Entries["x"] = Value::MakeFloat(it.Position.x);
                m->Entries["y"] = Value::MakeFloat(it.Position.y);
                m->Entries["z"] = Value::MakeFloat(it.Position.z);
                m->Entries["score"] = Value::MakeFloat(it.Score);
                m->Entries["entity"] = it.Entity == 0xFFFFFFFFu ? Value::MakeInt(-1) : Value::MakeInt((int64_t)it.Entity);
                list->Elements.push_back(Value::MakeObject(m));
            }
            return Value::MakeObject(list);
        };
        static std::vector<EnvQueryItem> s_LastQuery;
        vm->DefineNative("EQSQuery", [contextOf, itemsToValue](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity querier = Target(argc > 0 ? args[0] : Value::MakeNull());
            if (!scene || argc < 2 || !args[1].IsMap()) return itemsToValue({});
            const Value& p = args[1];
            EnvQueryDesc d;
            const std::string gen = Text(MapGet(p, "generator"));
            d.Generator.Type = gen == "ring" ? EnvGeneratorType::Ring : gen == "entities" ? EnvGeneratorType::Entities : EnvGeneratorType::Grid;
            if (auto v = MapGet(p, "radius"); v.IsNumber()) d.Generator.Radius = (float)v.ToNumber();
            if (auto v = MapGet(p, "spacing"); v.IsNumber()) d.Generator.Spacing = (float)v.ToNumber();
            if (auto v = MapGet(p, "rings"); v.IsNumber()) d.Generator.Rings = (int)v.ToNumber();
            if (auto v = MapGet(p, "points"); v.IsNumber()) d.Generator.PointsPerRing = (int)v.ToNumber();
            if (auto v = MapGet(p, "nav"); !v.IsNull()) d.Generator.ProjectToNavMesh = Truthy(v);
            d.Generator.Tag = Text(MapGet(p, "tag"));
            d.Generator.Center = contextOf(MapGet(p, "center"));
            if (auto v = MapGet(p, "max"); v.IsNumber()) d.MaxResults = (int)v.ToNumber();
            if (auto tests = MapGet(p, "tests"); tests.IsList())
                for (auto& tv : tests.AsList()->Elements)
                {
                    if (!tv.IsMap()) continue;
                    EnvTest t;
                    const std::string type = Text(MapGet(tv, "type"));
                    t.Type = type == "visibility" ? EnvTestType::Visibility : type == "dot" ? EnvTestType::Dot
                           : type == "path" ? EnvTestType::PathLength : type == "height" ? EnvTestType::Height : EnvTestType::Distance;
                    const std::string mode = Text(MapGet(tv, "mode"));
                    t.Mode = mode == "filter" ? EnvTestMode::Filter : mode == "both" ? EnvTestMode::FilterAndScore : EnvTestMode::Score;
                    t.Context = contextOf(MapGet(tv, "context"));
                    if (auto v = MapGet(tv, "min"); v.IsNumber()) t.FilterMin = (float)v.ToNumber();
                    if (auto v = MapGet(tv, "max"); v.IsNumber()) t.FilterMax = (float)v.ToNumber();
                    if (auto v = MapGet(tv, "visible"); !v.IsNull()) t.Visible = Truthy(v);
                    if (auto v = MapGet(tv, "weight"); v.IsNumber()) t.Weight = (float)v.ToNumber();
                    // "prefer": "near" / "low" -> düşük değer iyi
                    const std::string prefer = Text(MapGet(tv, "prefer"));
                    if ((prefer == "near" || prefer == "low") && t.Weight > 0.0f) t.Weight = -t.Weight;
                    d.Tests.push_back(t);
                }
            auto best = EnvQuery::Run(*scene, querier ? (uint32_t)(entt::entity)querier : 0xFFFFFFFFu, d, &s_LastQuery);
            if (auto v = MapGet(p, "debug"); v.IsNumber() && v.ToNumber() > 0.0)
                EnvQuery::DebugDraw(s_LastQuery, (float)v.ToNumber());
            return itemsToValue(best);
        }, 2);
        vm->DefineNative("EQSRunAsset", [itemsToValue](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1) return itemsToValue({});
            EnvQueryDesc d;
            std::string error;
            if (!EnvQuery::Load(Text(args[0]), d, &error)) return itemsToValue({});
            Entity querier = Target(argc > 1 ? args[1] : Value::MakeNull());
            Entity target = argc > 2 ? Target(args[2]) : Entity{};
            const uint32_t targetId = target ? (uint32_t)(entt::entity)target : 0xFFFFFFFFu;
            if (d.Generator.Center.Type == EnvContext::Kind::Entity) d.Generator.Center.EntityId = targetId;
            for (auto& t : d.Tests)
                if (t.Context.Type == EnvContext::Kind::Entity) t.Context.EntityId = targetId;
            return itemsToValue(EnvQuery::Run(*scene, querier ? (uint32_t)(entt::entity)querier : 0xFFFFFFFFu, d, &s_LastQuery));
        }, -1);
        vm->DefineNative("EQSDebugDraw", [](int argc, Value* args) -> Value {
            EnvQuery::DebugDraw(s_LastQuery, Num(argc, args, 0, 2.0f));
            return Value::MakeInt((int64_t)s_LastQuery.size());
        }, -1);

        // ================================================================ Godot sinyalleri
        auto entityId = [](Entity e) { return (uint32_t)(entt::entity)e; };
        vm->DefineNative("SignalConnect", [entityId](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 4) return Value::MakeBool(false);
            Entity source = Target(args[0]), target = Target(args[2]);
            if (!source || !target) return Value::MakeBool(false);
            return Value::MakeBool(Signals::Connect(*scene, entityId(source), Text(args[1]), entityId(target), Text(args[3]), argc > 4 && Truthy(args[4])));
        }, -1);
        vm->DefineNative("SignalDisconnect", [entityId](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 4) return Value::MakeBool(false);
            Entity source = Target(args[0]), target = Target(args[2]);
            if (!source || !target) return Value::MakeBool(false);
            return Value::MakeBool(Signals::Disconnect(*scene, entityId(source), Text(args[1]), entityId(target), Text(args[3])));
        }, 4);
        vm->DefineNative("SignalIsConnected", [entityId](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 4) return Value::MakeBool(false);
            Entity source = Target(args[0]), target = Target(args[2]);
            if (!source || !target) return Value::MakeBool(false);
            return Value::MakeBool(Signals::IsConnected(*scene, entityId(source), Text(args[1]), entityId(target), Text(args[3])));
        }, 4);
        vm->DefineNative("SignalEmit", [entityId](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 2) return Value::MakeInt(0);
            Entity source = Target(args[0]);
            if (!source) return Value::MakeInt(0);
            return Value::MakeInt(Signals::Emit(*scene, entityId(source), Text(args[1]), argc > 2 ? args + 2 : nullptr, std::max(argc - 2, 0)));
        }, -1);
        vm->DefineNative("SignalConnectionCount", [entityId](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity source = Target(argc > 0 ? args[0] : Value::MakeNull());
            if (!scene || !source || argc < 2) return Value::MakeInt(0);
            return Value::MakeInt(Signals::ConnectionCount(*scene, entityId(source), Text(args[1])));
        }, 2);

        // ================================================================ Tween
        vm->DefineNative("TweenTo", [entityId](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 4) return Value::MakeInt(0);
            Entity e = Target(args[0]);
            if (!e) return Value::MakeInt(0);
            TweenDesc d;
            d.Entity = entityId(e);
            d.Property = Text(args[1]);
            const Value& target = args[2];
            if (target.IsNumber()) { d.Target.x = (float)target.ToNumber(); d.Components = 1; }
            else if (target.IsList())
            {
                auto& l = target.AsList()->Elements;
                d.Components = (int)std::min<size_t>(l.size(), 4);
                for (int i = 0; i < d.Components; ++i) d.Target[i] = (float)l[(size_t)i].ToNumber();
            }
            else if (target.IsMap())
            {
                const char* keys[4][2] = { { "x", "r" }, { "y", "g" }, { "z", "b" }, { "w", "a" } };
                d.Components = 0;
                for (int i = 0; i < 4; ++i)
                {
                    Value v = MapGet(target, keys[i][0]);
                    if (v.IsNull()) v = MapGet(target, keys[i][1]);
                    if (v.IsNumber()) { d.Target[i] = (float)v.ToNumber(); d.Components = i + 1; }
                }
            }
            d.Duration = (float)args[3].ToNumber();
            if (argc > 4 && args[4].IsMap())
            {
                const Value& o = args[4];
                if (auto v = MapGet(o, "ease"); v.IsString()) d.Ease = Tweens::ParseEase(v.AsString()->Value);
                if (auto v = MapGet(o, "delay"); v.IsNumber()) d.Delay = (float)v.ToNumber();
                if (auto v = MapGet(o, "loops"); v.IsNumber()) d.Loops = (int)v.ToNumber();
                if (auto v = MapGet(o, "yoyo"); !v.IsNull()) d.Yoyo = Truthy(v);
                if (auto v = MapGet(o, "relative"); !v.IsNull()) d.Relative = Truthy(v);
                if (auto v = MapGet(o, "onComplete"); v.IsString()) d.OnComplete = v.AsString()->Value;
            }
            else if (argc > 4 && args[4].IsString())
                d.Ease = Tweens::ParseEase(args[4].AsString()->Value);
            return Value::MakeInt((int64_t)Tweens::Start(*scene, d));
        }, -1);
        vm->DefineNative("TweenKill", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1) return Value::MakeBool(false);
            return Value::MakeBool(Tweens::Kill(*scene, (uint32_t)args[0].ToInt(), argc > 1 && Truthy(args[1])));
        }, -1);
        vm->DefineNative("TweenKillAll", [entityId](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            if (!scene || !e) return Value::MakeInt(0);
            return Value::MakeInt(Tweens::KillAll(*scene, entityId(e)));
        }, -1);
        vm->DefineNative("TweenIsActive", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            return Value::MakeBool(scene && argc > 0 && Tweens::IsActive(*scene, (uint32_t)args[0].ToInt()));
        }, 1);
        vm->DefineNative("TweenPause", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (scene && argc > 0) Tweens::SetPaused(*scene, (uint32_t)args[0].ToInt(), argc < 2 || Truthy(args[1]));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("TweenCount", [](int, Value*) -> Value {
            Scene* scene = ActiveScene();
            return Value::MakeInt(scene ? Tweens::ActiveCount(*scene) : 0);
        }, 0);
        vm->DefineNative("Ease", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeFloat(0.0);
            return Value::MakeFloat(Tweens::Evaluate(Tweens::ParseEase(Text(args[0])), (float)args[1].ToNumber()));
        }, 2);

        // ================================================================ MultiMesh (GPU örnekleme)
        // Konum / dönüş (derece, Y) / ölçek ile örnek matrisi
        auto instanceMatrix = [](int argc, Value* args, int first) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(Num(argc, args, first, 0.0f), Num(argc, args, first + 1, 0.0f), Num(argc, args, first + 2, 0.0f)));
            const float yaw = Num(argc, args, first + 3, 0.0f);
            if (yaw != 0.0f)
                m = glm::rotate(m, glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));
            return glm::scale(m, glm::vec3(Num(argc, args, first + 4, 1.0f)));
        };
        auto multiMesh = [](const Value& v) -> MultiMeshComponent* {
            Entity e = Target(v);
            return e && e.HasComponent<MultiMeshComponent>() ? &e.GetComponent<MultiMeshComponent>() : nullptr;
        };
        vm->DefineNative("MultiMeshCount", [multiMesh](int argc, Value* args) -> Value {
            auto* c = multiMesh(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeInt(c ? (int64_t)c->Transforms.size() : 0);
        }, -1);
        vm->DefineNative("MultiMeshSetCount", [multiMesh](int argc, Value* args) -> Value {
            auto* c = multiMesh(argc > 0 ? args[0] : Value::MakeNull());
            if (!c || argc < 2) return Value::MakeBool(false);
            MultiMesh::SetCount(*c, (int)args[1].ToInt());
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("MultiMeshAdd", [multiMesh, instanceMatrix](int argc, Value* args) -> Value {
            auto* c = multiMesh(argc > 0 ? args[0] : Value::MakeNull());
            if (!c) return Value::MakeInt(-1);
            return Value::MakeInt(MultiMesh::AddInstance(*c, instanceMatrix(argc, args, 1)));
        }, -1);
        vm->DefineNative("MultiMeshSetTransform", [multiMesh, instanceMatrix](int argc, Value* args) -> Value {
            auto* c = multiMesh(argc > 0 ? args[0] : Value::MakeNull());
            if (!c || argc < 5) return Value::MakeBool(false);
            return Value::MakeBool(MultiMesh::SetInstance(*c, (int)args[1].ToInt(), instanceMatrix(argc, args, 2)));
        }, -1);
        vm->DefineNative("MultiMeshSetColor", [multiMesh](int argc, Value* args) -> Value {
            auto* c = multiMesh(argc > 0 ? args[0] : Value::MakeNull());
            if (!c || argc < 5) return Value::MakeBool(false);
            const glm::vec4 color(Num(argc, args, 2, 1.0f), Num(argc, args, 3, 1.0f), Num(argc, args, 4, 1.0f), Num(argc, args, 5, 1.0f));
            return Value::MakeBool(MultiMesh::SetInstanceColor(*c, (int)args[1].ToInt(), color));
        }, -1);
        vm->DefineNative("MultiMeshGetPosition", [multiMesh](int argc, Value* args) -> Value {
            auto* c = multiMesh(argc > 0 ? args[0] : Value::MakeNull());
            const int i = argc > 1 ? (int)args[1].ToInt() : -1;
            if (!c || i < 0 || i >= (int)c->Transforms.size()) return Value::MakeNull();
            const glm::vec3 p(c->Transforms[(size_t)i][3]);
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(p.x);
            m->Entries["y"] = Value::MakeFloat(p.y);
            m->Entries["z"] = Value::MakeFloat(p.z);
            return Value::MakeObject(m);
        }, 2);
        vm->DefineNative("MultiMeshClear", [multiMesh](int argc, Value* args) -> Value {
            auto* c = multiMesh(argc > 0 ? args[0] : Value::MakeNull());
            if (c) MultiMesh::Clear(*c);
            return Value::MakeBool(c != nullptr);
        }, -1);

        // ================================================================ Ray Cast / ekran bildirimi (Godot düğümleri)
        auto rayCast = [](const Value& v) -> RayCastComponent* {
            Entity e = Target(v);
            return e && e.HasComponent<RayCastComponent>() ? &e.GetComponent<RayCastComponent>() : nullptr;
        };
        auto vecMap = [](const glm::vec3& p) -> Value {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(p.x);
            m->Entries["y"] = Value::MakeFloat(p.y);
            m->Entries["z"] = Value::MakeFloat(p.z);
            return Value::MakeObject(m);
        };
        vm->DefineNative("RayCastIsColliding", [rayCast](int argc, Value* args) -> Value {
            auto* rc = rayCast(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(rc && rc->Colliding);
        }, -1);
        vm->DefineNative("RayCastGetPoint", [rayCast, vecMap](int argc, Value* args) -> Value {
            auto* rc = rayCast(argc > 0 ? args[0] : Value::MakeNull());
            return rc && rc->Colliding ? vecMap(rc->CollisionPoint) : Value::MakeNull();
        }, -1);
        vm->DefineNative("RayCastGetNormal", [rayCast, vecMap](int argc, Value* args) -> Value {
            auto* rc = rayCast(argc > 0 ? args[0] : Value::MakeNull());
            return rc && rc->Colliding ? vecMap(rc->CollisionNormal) : Value::MakeNull();
        }, -1);
        vm->DefineNative("RayCastGetCollider", [rayCast](int argc, Value* args) -> Value {
            auto* rc = rayCast(argc > 0 ? args[0] : Value::MakeNull());
            return rc && rc->Colliding ? Value::MakeInt((int64_t)rc->ColliderEntity) : Value::MakeNull();
        }, -1);
        vm->DefineNative("RayCastGetDistance", [rayCast](int argc, Value* args) -> Value {
            auto* rc = rayCast(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeFloat(rc && rc->Colliding ? rc->Distance : -1.0);
        }, -1);
        vm->DefineNative("RayCastSetTarget", [rayCast](int argc, Value* args) -> Value {
            auto* rc = rayCast(argc > 0 ? args[0] : Value::MakeNull());
            if (!rc || argc < 3) return Value::MakeBool(false);
            rc->TargetPosition = glm::vec3(Num(argc, args, 1, 0.0f), Num(argc, args, 2, 0.0f), Num(argc, args, 3, 0.0f));
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("RayCastForceUpdate", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(scene && e && GodotNodes::ForceRayCast(*scene, (uint32_t)(entt::entity)e));
        }, -1);
        // ================================================================ Shape Cast (Godot)
        auto shapeCast = [](const Value& v) -> ShapeCastComponent* {
            Entity e = Target(v);
            return e && e.HasComponent<ShapeCastComponent>() ? &e.GetComponent<ShapeCastComponent>() : nullptr;
        };
        auto shapeResult = [shapeCast](int argc, Value* args) -> const ShapeCastComponent::Result* {
            auto* sc = shapeCast(argc > 0 ? args[0] : Value::MakeNull());
            const int i = argc > 1 ? (int)args[1].ToNumber() : 0;
            return sc && i >= 0 && i < (int)sc->Results.size() ? &sc->Results[(size_t)i] : nullptr;
        };
        vm->DefineNative("ShapeCastIsColliding", [shapeCast](int argc, Value* args) -> Value {
            auto* sc = shapeCast(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(sc && sc->Colliding);
        }, -1);
        vm->DefineNative("ShapeCastGetCollisionCount", [shapeCast](int argc, Value* args) -> Value {
            auto* sc = shapeCast(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeInt(sc ? sc->CollisionCount : 0);
        }, -1);
        vm->DefineNative("ShapeCastGetPoint", [shapeResult, vecMap](int argc, Value* args) -> Value {
            const auto* r = shapeResult(argc, args);
            return r ? vecMap(r->Point) : Value::MakeNull();
        }, -1);
        vm->DefineNative("ShapeCastGetNormal", [shapeResult, vecMap](int argc, Value* args) -> Value {
            const auto* r = shapeResult(argc, args);
            return r ? vecMap(r->Normal) : Value::MakeNull();
        }, -1);
        vm->DefineNative("ShapeCastGetCollider", [shapeResult](int argc, Value* args) -> Value {
            const auto* r = shapeResult(argc, args);
            return r ? Value::MakeInt((int64_t)r->Entity) : Value::MakeNull();
        }, -1);
        vm->DefineNative("ShapeCastGetSafeFraction", [shapeCast](int argc, Value* args) -> Value {
            auto* sc = shapeCast(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeFloat(sc ? sc->SafeFraction : 1.0);
        }, -1);
        vm->DefineNative("ShapeCastSetTarget", [shapeCast](int argc, Value* args) -> Value {
            auto* sc = shapeCast(argc > 0 ? args[0] : Value::MakeNull());
            if (!sc || argc < 3) return Value::MakeBool(false);
            sc->TargetPosition = glm::vec3(Num(argc, args, 1, 0.0f), Num(argc, args, 2, 0.0f), Num(argc, args, 3, 0.0f));
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("ShapeCastForceUpdate", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(scene && e && GodotNodes::ForceShapeCast(*scene, (uint32_t)(entt::entity)e));
        }, -1);

        // ================================================================ Character Body 2D (Godot)
        auto body2d = [](const Value& v) -> CharacterBody2DComponent* {
            Entity e = Target(v);
            return e && e.HasComponent<CharacterBody2DComponent>() ? &e.GetComponent<CharacterBody2DComponent>() : nullptr;
        };
        auto vec2Map = [](const glm::vec2& p) -> Value {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(p.x);
            m->Entries["y"] = Value::MakeFloat(p.y);
            return Value::MakeObject(m);
        };
        vm->DefineNative("CharacterBody2DMoveAndSlide", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            const float dt = argc > 1 ? (float)args[1].ToNumber() : -1.0f;
            return Value::MakeBool(scene && e && GodotNodes::MoveAndSlide2D(*scene, (uint32_t)(entt::entity)e, dt));
        }, -1);
        vm->DefineNative("CharacterBody2DSetVelocity", [body2d](int argc, Value* args) -> Value {
            auto* cb = body2d(argc > 0 ? args[0] : Value::MakeNull());
            if (!cb || argc < 3) return Value::MakeBool(false);
            cb->Velocity = glm::vec2(Num(argc, args, 1, 0.0f), Num(argc, args, 2, 0.0f));
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("CharacterBody2DGetVelocity", [body2d, vec2Map](int argc, Value* args) -> Value {
            auto* cb = body2d(argc > 0 ? args[0] : Value::MakeNull());
            return cb ? vec2Map(cb->Velocity) : Value::MakeNull();
        }, -1);
        vm->DefineNative("CharacterBody2DGetRealVelocity", [body2d, vec2Map](int argc, Value* args) -> Value {
            auto* cb = body2d(argc > 0 ? args[0] : Value::MakeNull());
            return cb ? vec2Map(cb->RealVelocity) : Value::MakeNull();
        }, -1);
        vm->DefineNative("CharacterBody2DIsOnFloor", [body2d](int argc, Value* args) -> Value {
            auto* cb = body2d(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(cb && cb->OnFloor);
        }, -1);
        vm->DefineNative("CharacterBody2DIsOnWall", [body2d](int argc, Value* args) -> Value {
            auto* cb = body2d(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(cb && cb->OnWall);
        }, -1);
        vm->DefineNative("CharacterBody2DIsOnCeiling", [body2d](int argc, Value* args) -> Value {
            auto* cb = body2d(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(cb && cb->OnCeiling);
        }, -1);
        vm->DefineNative("CharacterBody2DGetFloorNormal", [body2d, vec2Map](int argc, Value* args) -> Value {
            auto* cb = body2d(argc > 0 ? args[0] : Value::MakeNull());
            return cb && cb->OnFloor ? vec2Map(cb->FloorNormal) : Value::MakeNull();
        }, -1);
        vm->DefineNative("CharacterBody2DGetWallNormal", [body2d, vec2Map](int argc, Value* args) -> Value {
            auto* cb = body2d(argc > 0 ? args[0] : Value::MakeNull());
            return cb && cb->OnWall ? vec2Map(cb->WallNormal) : Value::MakeNull();
        }, -1);

        // ================================================================ Timer (Godot)
        auto timer = [](const Value& v) -> TimerComponent* {
            Entity e = Target(v);
            return e && e.HasComponent<TimerComponent>() ? &e.GetComponent<TimerComponent>() : nullptr;
        };
        // ================================================================ Kalıcı nesneler (Unity DontDestroyOnLoad, Godot Autoload)
        vm->DefineNative("DontDestroyOnLoad", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(scene && e && scene->SetPersistent(e, true));
        }, -1);
        vm->DefineNative("IsPersistent", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(scene && e && scene->IsPersistent(e));
        }, -1);
        vm->DefineNative("GetAutoload", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1 || !args[0].IsString()) return Value::MakeNull();
            const uint32_t id = GodotNodes::GetAutoload(*scene, args[0].AsString()->Value);
            return id != UINT32_MAX ? Value::MakeInt((int64_t)id) : Value::MakeNull();
        }, 1);
        // Window (Godot): ayrı işletim sistemi penceresi. Hedef verilmezse betiğin nesnesi
        auto gameWindow = [](const Value& v) -> GameWindowComponent* {
            Scene* scene = ActiveScene();
            Entity e = Target(v);
            return scene && e && e.HasComponent<GameWindowComponent>() ? &e.GetComponent<GameWindowComponent>() : nullptr;
        };
        vm->DefineNative("GameWindowShow", [gameWindow](int argc, Value* args) -> Value {
            auto* w = gameWindow(argc > 0 ? args[0] : Value::MakeNull());
            if (w) w->Visible = true;
            return Value::MakeBool(w != nullptr);
        }, -1);
        vm->DefineNative("GameWindowHide", [gameWindow](int argc, Value* args) -> Value {
            auto* w = gameWindow(argc > 0 ? args[0] : Value::MakeNull());
            if (w) w->Visible = false;
            return Value::MakeBool(w != nullptr);
        }, -1);
        vm->DefineNative("GameWindowIsVisible", [gameWindow](int argc, Value* args) -> Value {
            auto* w = gameWindow(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(w && w->Visible && w->Open);
        }, -1);
        vm->DefineNative("GameWindowSetTitle", [gameWindow](int argc, Value* args) -> Value {
            auto* w = gameWindow(argc >= 2 ? args[0] : Value::MakeNull());
            if (w && argc >= 1) w->Title = args[argc >= 2 ? 1 : 0].ToString();
            return Value::MakeBool(w != nullptr);
        }, -1);
        vm->DefineNative("GameWindowSetSize", [gameWindow](int argc, Value* args) -> Value {
            auto* w = gameWindow(argc >= 3 ? args[0] : Value::MakeNull());
            const int o = argc >= 3 ? 1 : 0;
            if (w && argc >= 2) w->Size = glm::vec2(std::max(1.0f, (float)args[o].ToNumber()), std::max(1.0f, (float)args[o + 1].ToNumber()));
            return Value::MakeBool(w != nullptr);
        }, -1);
        vm->DefineNative("GameWindowGetSize", [gameWindow](int argc, Value* args) -> Value {
            auto* w = gameWindow(argc > 0 ? args[0] : Value::MakeNull());
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(w ? w->Size.x : 0.0f);
            m->Entries["y"] = Value::MakeFloat(w ? w->Size.y : 0.0f);
            return Value::MakeObject(m);
        }, -1);
        vm->DefineNative("GameWindowSetPosition", [gameWindow](int argc, Value* args) -> Value {
            auto* w = gameWindow(argc >= 3 ? args[0] : Value::MakeNull());
            const int o = argc >= 3 ? 1 : 0;
            if (w && argc >= 2) w->Position = glm::vec2((float)args[o].ToNumber(), (float)args[o + 1].ToNumber());
            return Value::MakeBool(w != nullptr);
        }, -1);
        vm->DefineNative("GameWindowGrabFocus", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(scene && e && GameWindows::GrabFocus(*scene, (uint32_t)(entt::entity)e));
        }, -1);
        vm->DefineNative("GameWindowHasFocus", [gameWindow](int argc, Value* args) -> Value {
            auto* w = gameWindow(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(w && w->Focused);
        }, -1);
        // Resource Preloader (Godot): adıyla yol / adlar / ekle / kaldır. Hedef verilmezse betiğin nesnesi
        vm->DefineNative("PreloaderGetResource", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = argc >= 2 ? Target(args[0]) : Target(Value::MakeNull());
            if (!scene || !e || argc < 1) return Value::MakeNull();
            const std::string path = GodotNodes::PreloaderGet(*scene, (uint32_t)(entt::entity)e, args[argc >= 2 ? 1 : 0].ToString());
            return path.empty() ? Value::MakeNull() : Value::MakeString(path);
        }, -1);
        vm->DefineNative("PreloaderHasResource", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = argc >= 2 ? Target(args[0]) : Target(Value::MakeNull());
            return Value::MakeBool(scene && e && argc >= 1 && !GodotNodes::PreloaderGet(*scene, (uint32_t)(entt::entity)e, args[argc >= 2 ? 1 : 0].ToString()).empty());
        }, -1);
        vm->DefineNative("PreloaderGetResourceList", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            auto* list = new CQSListObject();
            if (scene && e)
                for (const auto& n : GodotNodes::PreloaderNames(*scene, (uint32_t)(entt::entity)e))
                    list->Elements.push_back(Value::MakeString(n));
            return Value::MakeObject(list);
        }, -1);
        vm->DefineNative("PreloaderAddResource", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = argc >= 3 ? Target(args[0]) : Target(Value::MakeNull());
            const int o = argc >= 3 ? 1 : 0;
            return Value::MakeBool(scene && e && argc >= 2 && GodotNodes::PreloaderAdd(*scene, (uint32_t)(entt::entity)e, args[o].ToString(), args[o + 1].ToString()));
        }, -1);
        vm->DefineNative("PreloaderRemoveResource", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = argc >= 2 ? Target(args[0]) : Target(Value::MakeNull());
            return Value::MakeBool(scene && e && argc >= 1 && GodotNodes::PreloaderRemove(*scene, (uint32_t)(entt::entity)e, args[argc >= 2 ? 1 : 0].ToString()));
        }, -1);
        vm->DefineNative("TimerStart", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            const float time = argc > 1 ? (float)args[1].ToNumber() : -1.0f;
            return Value::MakeBool(scene && e && GodotNodes::TimerStart(*scene, (uint32_t)(entt::entity)e, time));
        }, -1);
        vm->DefineNative("TimerStop", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(scene && e && GodotNodes::TimerStop(*scene, (uint32_t)(entt::entity)e));
        }, -1);
        vm->DefineNative("TimerIsStopped", [timer](int argc, Value* args) -> Value {
            auto* t = timer(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(!t || !t->Running);
        }, -1);
        vm->DefineNative("TimerGetTimeLeft", [timer](int argc, Value* args) -> Value {
            auto* t = timer(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeFloat(t && t->Running ? std::max(t->TimeLeft, 0.0f) : 0.0);
        }, -1);
        vm->DefineNative("TimerSetPaused", [timer](int argc, Value* args) -> Value {
            auto* t = timer(argc > 0 ? args[0] : Value::MakeNull());
            if (!t || argc < 2) return Value::MakeBool(false);
            t->Paused = args[1].IsBool() ? args[1].AsBool() : args[1].ToNumber() != 0.0;
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("TimerSetWaitTime", [timer](int argc, Value* args) -> Value {
            auto* t = timer(argc > 0 ? args[0] : Value::MakeNull());
            if (!t || argc < 2) return Value::MakeBool(false);
            t->WaitTime = std::max((float)args[1].ToNumber(), 0.0f);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("IsOnScreen", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(e && e.HasComponent<VisibleOnScreenNotifierComponent>() && e.GetComponent<VisibleOnScreenNotifierComponent>().OnScreen);
        }, -1);

        // ================================================================ Ragdoll
        vm->DefineNative("RagdollActivate", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            if (!scene || !e) return Value::MakeBool(false);
            const glm::vec3 impulse(Num(argc, args, 1, 0.0f), Num(argc, args, 2, 0.0f), Num(argc, args, 3, 0.0f));
            const glm::vec3 point(Num(argc, args, 4, 0.0f), Num(argc, args, 5, 0.0f), Num(argc, args, 6, 0.0f));
            // Nokta verilmezse itme tüm gövdeye (kütleye oranla) dağıtılır
            return Value::MakeBool(Ragdoll::Activate(*scene, (uint32_t)(entt::entity)e, impulse, point, argc <= 4));
        }, -1);
        vm->DefineNative("RagdollDeactivate", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            if (!scene || !e) return Value::MakeBool(false);
            Ragdoll::Deactivate(*scene, (uint32_t)(entt::entity)e);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("RagdollIsActive", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(e && e.HasComponent<RagdollComponent>() && e.GetComponent<RagdollComponent>().Simulating);
        }, -1);
        vm->DefineNative("RagdollGetBodyCount", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeInt(e && e.HasComponent<RagdollComponent>() ? e.GetComponent<RagdollComponent>().BodyCount : 0);
        }, -1);

        // ================================================================ Fracture (Chaos destruction)
        vm->DefineNative("FractureBreak", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            if (!scene || !e) return Value::MakeInt(0);
            if (!e.HasComponent<FractureComponent>()) e.AddComponent<FractureComponent>();
            glm::vec3 point = glm::vec3(e.GetComponent<TransformComponent>().WorldTransform[3]);
            if (argc >= 4) point = glm::vec3(Num(argc, args, 1, point.x), Num(argc, args, 2, point.y), Num(argc, args, 3, point.z));
            return Value::MakeInt(Fracture::Break(*scene, (uint32_t)(entt::entity)e, point, Num(argc, args, 4, 0.0f)));
        }, -1);
        vm->DefineNative("FractureIsBroken", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeBool(!e || (e.HasComponent<FractureComponent>() && e.GetComponent<FractureComponent>().Broken));
        }, -1);

        // ================================================================ Mesh Paint (köşe renkleri)
        auto colorOf = [](const Value& v, glm::vec4 fallback) -> glm::vec4 {
            if (v.IsMap())
            {
                auto& m = v.AsMap()->Entries;
                auto get = [&](const char* a, const char* b, float d) {
                    auto it = m.find(a);
                    if (it == m.end()) it = m.find(b);
                    return it != m.end() ? (float)it->second.ToNumber() : d;
                };
                return glm::vec4(get("r", "x", fallback.r), get("g", "y", fallback.g), get("b", "z", fallback.b), get("a", "w", 1.0f));
            }
            if (v.IsList())
            {
                auto& l = v.AsList()->Elements;
                glm::vec4 c = fallback;
                for (int i = 0; i < 4 && i < (int)l.size(); ++i)
                    c[i] = (float)l[(size_t)i].ToNumber();
                return c;
            }
            return fallback;
        };
        vm->DefineNative("VertexPaintSphere", [colorOf](int argc, Value* args) -> Value {
            if (argc < 6) return Value::MakeInt(0);
            Entity e = Target(args[0]);
            const glm::vec3 center(Num(argc, args, 1, 0.0f), Num(argc, args, 2, 0.0f), Num(argc, args, 3, 0.0f));
            return Value::MakeInt(VertexPaint::PaintSphere(e, center, Num(argc, args, 4, 0.5f), colorOf(args[5], glm::vec4(1.0f)),
                                                           Num(argc, args, 6, 1.0f), Num(argc, args, 7, 0.5f)));
        }, -1);
        vm->DefineNative("VertexPaintFill", [colorOf](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            Entity e = Target(args[0]);
            if (!e) return Value::MakeBool(false);
            VertexPaint::Fill(e, colorOf(args[1], glm::vec4(1.0f)));
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("VertexPaintGetColor", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            const glm::vec4 c = VertexPaint::GetColor(e, (int)Num(argc, args, 1, 0.0f), (int)Num(argc, args, 2, 0.0f));
            auto* list = new CQSListObject();
            for (int i = 0; i < 4; ++i) list->Elements.push_back(Value::MakeFloat(c[i]));
            return Value::MakeObject(list);
        }, -1);

        // ================================================================ Auto LOD
        vm->DefineNative("MeshGenerateLODs", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            Scene* scene = ActiveScene();
            if (!e || !scene || !e.HasComponent<MeshRendererComponent>() || !e.GetComponent<MeshRendererComponent>().ModelData) return Value::MakeInt(0);
            if (!e.HasComponent<AutoLODComponent>()) e.AddComponent<AutoLODComponent>();
            auto& c = e.GetComponent<AutoLODComponent>();
            if (argc > 1 && args[1].IsNumber()) c.Levels = std::clamp((int)args[1].ToNumber(), 1, 4);
            if (argc > 2 && args[2].IsNumber()) c.Reduction = std::clamp((float)args[2].ToNumber(), 0.05f, 0.95f);
            return Value::MakeInt(AutoLOD::GenerateNow(*scene, (uint32_t)(entt::entity)e));
        }, -1);
        vm->DefineNative("MeshGetLODTriangles", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            auto* list = new CQSListObject();
            if (!e || !e.HasComponent<MeshRendererComponent>() || !e.GetComponent<MeshRendererComponent>().ModelData) return Value::MakeObject(list);
            const Model& model = *e.GetComponent<MeshRendererComponent>().ModelData;
            int levels = 1;
            for (const auto& m : model.Meshes) if (m) levels = std::max(levels, m->GetLODCount());
            for (int l = 0; l < levels; ++l)
            {
                int64_t tris = 0;
                for (const auto& m : model.Meshes) if (m) tris += m->GetLOD(l)->GetIndexCount() / 3;
                list->Elements.push_back(Value::MakeInt(tris));
            }
            return Value::MakeObject(list);
        }, -1);
        vm->DefineNative("MeshGetCurrentLOD", [](int argc, Value* args) -> Value {
            Entity e = Target(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeInt(e && e.HasComponent<AutoLODComponent>() ? e.GetComponent<AutoLODComponent>().CurrentLOD : 0);
        }, -1);

        vm->DefineNative("AnimGetNotifyCount", [](int argc, Value* args) -> Value {
            Entity e = AnimTarget(argc > 0 ? args[0] : Value::MakeNull());
            return Value::MakeInt(e ? (int64_t)e.GetComponent<AnimationComponent>().NotifyCount : 0);
        }, -1);
    }
}
