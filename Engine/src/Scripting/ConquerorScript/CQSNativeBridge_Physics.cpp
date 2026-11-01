#include "CQSNativeBridge.h"
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scene/Scene.h"
#include "Scene/ScenePicking.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Math/Math.h"
#include "Scene/Splines.h"
#include "Scene/CameraRig.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

// Fizik sorguları, karakter denetleyicisi, temas bilgisi, genel bileşen alanları ve
// varlık (ID ile) işlemleri — tüm betik dilleri nativebridge üzerinden kullanır.
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

        // Varlık argümanı: kimlik (sayı) ya da ad (metin). Ad bulunamazsa boş varlık (eskiden metin 0'a
        // çevrilip 0 numaralı varlık değişiyordu).
        Entity EntityFromValue(const Value& v)
        {
            if (!v.IsString())
                return EntityFromId((int64_t)v.ToNumber());
            Scene* scene = ScriptContext::GetActiveScene();
            if (!scene)
                return {};
            const std::string& name = v.AsString()->Value;
            auto view = scene->m_Registry.view<TagComponent>();
            for (auto h : view)
                if (view.get<TagComponent>(h).Tag == name)
                    return Entity{ h, scene };
            return {};
        }

        Entity Current()
        {
            uint32_t id = ScriptContext::GetCurrentEntity();
            return id == ScriptContext::NullEntity ? Entity{} : EntityFromId(id);
        }

        PhysicsWorld2D* World2D()
        {
            Scene* scene = ActiveScene();
            return scene ? scene->GetPhysicsWorld2D() : nullptr;
        }

        Value Vec2(const glm::vec2& v)
        {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(v.x);
            m->Entries["y"] = Value::MakeFloat(v.y);
            return Value::MakeObject(m);
        }

        PhysicsWorld3D* World()
        {
            Scene* scene = ActiveScene();
            return scene ? scene->GetPhysicsWorld3D() : nullptr;
        }

        float Num(int argc, Value* args, int i, float def = 0.0f)
        {
            return i < argc && args[i].IsNumber() ? (float)args[i].ToNumber() : def;
        }

        Value Vec3(const glm::vec3& v)
        {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(v.x);
            m->Entries["y"] = Value::MakeFloat(v.y);
            m->Entries["z"] = Value::MakeFloat(v.z);
            return Value::MakeObject(m);
        }

        // {x,y,z} haritası, [x,y,z] listesi veya tek sayı
        bool ReadVec(const Value& v, float* out, int n)
        {
            if (v.IsMap())
            {
                static const char* keys[] = { "x", "y", "z", "w" };
                auto& e = v.AsMap()->Entries;
                for (int i = 0; i < n; ++i)
                {
                    auto it = e.find(keys[i]);
                    if (it == e.end())
                    {
                        static const char* colorKeys[] = { "r", "g", "b", "a" };
                        it = e.find(colorKeys[i]);
                    }
                    out[i] = it != e.end() ? (float)it->second.ToNumber() : 0.0f;
                }
                return true;
            }
            if (v.IsList())
            {
                auto& l = v.AsList()->Elements;
                for (int i = 0; i < n; ++i) out[i] = i < (int)l.size() ? (float)l[(size_t)i].ToNumber() : 0.0f;
                return true;
            }
            if (v.IsNumber())
            {
                for (int i = 0; i < n; ++i) out[i] = (float)v.ToNumber();
                return true;
            }
            return false;
        }

        uint32_t Mask(int argc, Value* args, int i)
        {
            if (i < argc && args[i].IsNumber())
            {
                int64_t m = args[i].IsInt() ? args[i].AsInt() : (int64_t)args[i].ToNumber();
                return m < 0 ? 0xFFFFFFFFu : (uint32_t)m;
            }
            return 0xFFFFFFFFu;
        }

        // İsteğe bağlı "ignoreSelf" argümanı: doğruysa çağıran betiğin varlığı sorgudan hariç tutulur
        uint32_t IgnoreSelf(int argc, Value* args, int index)
        {
            if (argc <= index)
                return Physics3D::NoEntity;
            const Value& v = args[index];
            const bool on = v.IsBool() ? v.AsBool() : (v.IsNumber() && v.ToNumber() != 0.0);
            Entity self = on ? Current() : Entity{};
            return self ? (uint32_t)self : Physics3D::NoEntity;
        }

        Value HitToValue(const Physics3D::RaycastHit& h)
        {
            auto* m = new CQSMapObject();
            m->Entries["entity"] = Value::MakeInt((int64_t)h.Entity);
            Entity e = EntityFromId(h.Entity);
            m->Entries["name"] = Value::MakeString(e && e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Tag : "");
            m->Entries["point"] = Vec3(h.Point);
            m->Entries["normal"] = Vec3(h.Normal);
            m->Entries["distance"] = Value::MakeFloat(h.Distance);
            m->Entries["isTrigger"] = Value::MakeBool(h.IsTrigger);
            return Value::MakeObject(m);
        }

        Value EntityList(const std::vector<uint32_t>& ids)
        {
            auto* l = new CQSListObject();
            for (uint32_t id : ids) l->Elements.push_back(Value::MakeInt((int64_t)id));
            return Value::MakeObject(l);
        }

        // Bileşen alanı <-> betik değeri
        // Erişimcili (Ptr'siz) alanın metnini türüne göre betik değerine çevir
        Value AccessorFieldValue(const ComponentField& f, void* component)
        {
            if (!f.GetString)
                return Value::MakeNull();
            const std::string s = f.GetString(component);
            switch (f.Kind)
            {
                case FieldKind::Bool: return Value::MakeBool(s == "true" || s == "1");
                case FieldKind::Int: return Value::MakeInt(std::atoll(s.c_str()));
                case FieldKind::Float:
                case FieldKind::Angle: return Value::MakeFloat(std::atof(s.c_str()));
                case FieldKind::Vec2:
                case FieldKind::Vec3:
                case FieldKind::Vec4:
                case FieldKind::Color3:
                case FieldKind::Color4:
                {
                    const bool color = f.Kind == FieldKind::Color3 || f.Kind == FieldKind::Color4;
                    const char* keys = color ? "rgba" : "xyzw";
                    std::string t = s;
                    std::replace(t.begin(), t.end(), ',', ' ');
                    std::istringstream in(t);
                    auto* m = new CQSMapObject();
                    float v = 0.0f;
                    for (int i = 0; i < 4 && (in >> v); ++i)
                        m->Entries[std::string(1, keys[i])] = Value::MakeFloat(v);
                    return Value::MakeObject(m);
                }
                default: return Value::MakeString(s);
            }
        }

        Value FieldValue(const ComponentField& f, void* component)
        {
            if (!f.Ptr)
                return AccessorFieldValue(f, component);
            void* p = f.Ptr(component);
            switch (f.Kind)
            {
                case FieldKind::Bool: return Value::MakeBool(*(bool*)p);
                case FieldKind::Int: return Value::MakeInt(*(int*)p);
                case FieldKind::Float:
                case FieldKind::Angle: return Value::MakeFloat(*(float*)p);
                case FieldKind::Vec2:
                {
                    auto* m = new CQSMapObject();
                    m->Entries["x"] = Value::MakeFloat(((float*)p)[0]);
                    m->Entries["y"] = Value::MakeFloat(((float*)p)[1]);
                    return Value::MakeObject(m);
                }
                case FieldKind::Vec3: return Vec3(*(glm::vec3*)p);
                case FieldKind::Color3:
                case FieldKind::Color4:
                case FieldKind::Vec4:
                {
                    int n = f.Kind == FieldKind::Color3 ? 3 : 4;
                    const char* keys = (f.Kind == FieldKind::Vec4) ? "xyzw" : "rgba";
                    auto* m = new CQSMapObject();
                    for (int i = 0; i < n; ++i) m->Entries[std::string(1, keys[i])] = Value::MakeFloat(((float*)p)[i]);
                    return Value::MakeObject(m);
                }
                case FieldKind::String:
                case FieldKind::Asset: return Value::MakeString(*(std::string*)p);
                case FieldKind::Enum: return Value::MakeString(ComponentRegistry::FieldToString(f, component));
                case FieldKind::EntityRef:
                {
                    // Betikler varlık kimliğiyle çalışır: saklanan UUID -> varlık (yoksa null)
                    uint64_t uuid = *(uint64_t*)p;
                    Scene* scene = ActiveScene();
                    Entity e = (uuid && scene) ? scene->FindEntityByUUID(uuid) : Entity{};
                    return e ? Value::MakeInt((int64_t)(uint32_t)(entt::entity)e) : Value::MakeNull();
                }
                case FieldKind::LayerMask: return Value::MakeInt((int64_t)*(uint32_t*)p);
                case FieldKind::Vec2List:
                {
                    auto* l = new CQSListObject();
                    for (auto& v : *(std::vector<glm::vec2>*)p)
                    {
                        auto* m = new CQSMapObject();
                        m->Entries["x"] = Value::MakeFloat(v.x);
                        m->Entries["y"] = Value::MakeFloat(v.y);
                        l->Elements.push_back(Value::MakeObject(m));
                    }
                    return Value::MakeObject(l);
                }
                case FieldKind::Vec3List:
                {
                    auto* l = new CQSListObject();
                    for (auto& v : *(std::vector<glm::vec3>*)p)
                        l->Elements.push_back(Vec3(v));
                    return Value::MakeObject(l);
                }
            }
            return Value::MakeNull();
        }

        bool SetFieldValue(const ComponentField& f, void* component, const Value& v)
        {
            if (f.ReadOnly)
                return false;
            if (!f.Ptr)
            {
                if (!f.SetString)
                    return false;
                // Türüne göre metin: bool -> true/false, vektör/renk haritası -> "x y z"
                std::string text;
                int n = f.Kind == FieldKind::Vec2 ? 2 : (f.Kind == FieldKind::Vec3 || f.Kind == FieldKind::Color3) ? 3
                      : (f.Kind == FieldKind::Vec4 || f.Kind == FieldKind::Color4) ? 4 : 0;
                if (n > 0 && (v.IsMap() || v.IsList()))
                {
                    float tmp[4] = { 0, 0, 0, 1 };
                    ReadVec(v, tmp, n);
                    for (int i = 0; i < n; ++i)
                        text += (i ? " " : "") + std::to_string(tmp[i]);
                }
                else if (v.IsBool())
                    text = v.AsBool() ? "true" : "false";
                else if (v.IsNumber())
                    text = v.IsInt() ? std::to_string(v.AsInt()) : std::to_string(v.ToNumber());
                else
                    text = v.IsNull() ? std::string() : v.ToString();
                f.SetString(component, text);
                return true;
            }
            void* p = f.Ptr(component);
            switch (f.Kind)
            {
                case FieldKind::Bool: *(bool*)p = v.IsBool() ? v.AsBool() : v.ToNumber() != 0.0; return true;
                case FieldKind::Int: *(int*)p = (int)v.ToNumber(); return true;
                case FieldKind::Float:
                case FieldKind::Angle: *(float*)p = (float)v.ToNumber(); return true;
                case FieldKind::Vec2: return ReadVec(v, (float*)p, 2);
                case FieldKind::Vec3:
                case FieldKind::Color3: return ReadVec(v, (float*)p, 3);
                case FieldKind::Vec4:
                case FieldKind::Color4: return ReadVec(v, (float*)p, 4);
                case FieldKind::String:
                case FieldKind::Asset: *(std::string*)p = v.ToString(); return true;
                case FieldKind::Enum:
                    return ComponentRegistry::FieldFromString(f, component, v.IsString() ? v.ToString() : std::to_string((int)v.ToNumber()));
                case FieldKind::EntityRef:
                {
                    // Varlık kimliği (FindEntity sonucu) -> UUID; null / -1 temizler
                    if (v.IsNull() || v.ToNumber() < 0.0)
                    {
                        *(uint64_t*)p = 0;
                        return true;
                    }
                    int64_t id = (int64_t)v.ToNumber();
                    Scene* scene = ActiveScene();
                    if (scene && id <= 0xFFFFFFFFll && scene->m_Registry.valid((entt::entity)(uint32_t)id))
                    {
                        Entity e{ (entt::entity)(uint32_t)id, scene };
                        if (e.HasComponent<IDComponent>())
                        {
                            *(uint64_t*)p = (uint64_t)e.GetComponent<IDComponent>().ID;
                            return true;
                        }
                    }
                    *(uint64_t*)p = (uint64_t)id;   // doğrudan UUID verildiyse
                    return true;
                }
                case FieldKind::LayerMask: *(uint32_t*)p = (uint32_t)(int64_t)v.ToNumber(); return true;
                case FieldKind::Vec2List:
                {
                    if (!v.IsList()) return false;
                    std::vector<glm::vec2> out;
                    for (auto& e : v.AsList()->Elements)
                    {
                        float xy[2] = { 0, 0 };
                        if (ReadVec(e, xy, 2)) out.emplace_back(xy[0], xy[1]);
                    }
                    *(std::vector<glm::vec2>*)p = std::move(out);
                    return true;
                }
                case FieldKind::Vec3List:
                {
                    if (!v.IsList()) return false;
                    std::vector<glm::vec3> out;
                    for (auto& e : v.AsList()->Elements)
                    {
                        float xyz[3] = { 0, 0, 0 };
                        if (ReadVec(e, xyz, 3)) out.emplace_back(xyz[0], xyz[1], xyz[2]);
                    }
                    *(std::vector<glm::vec3>*)p = std::move(out);
                    return true;
                }
            }
            return false;
        }

        Value GetField(Entity e, const Value& comp, const Value& field)
        {
            if (!e) return Value::MakeNull();
            ComponentInfo* info = ComponentRegistry::Find(comp.ToString());
            if (!info || !info->Has(e)) return Value::MakeNull();
            const ComponentField* f = ComponentRegistry::FindField(*info, field.ToString());
            return f ? FieldValue(*f, info->Get(e)) : Value::MakeNull();
        }

        Value SetField(Entity e, const Value& comp, const Value& field, const Value& value)
        {
            if (!e) return Value::MakeBool(false);
            ComponentInfo* info = ComponentRegistry::Find(comp.ToString());
            if (!info || !info->Has(e)) return Value::MakeBool(false);
            const ComponentField* f = ComponentRegistry::FindField(*info, field.ToString());
            if (!f || !SetFieldValue(*f, info->Get(e), value)) return Value::MakeBool(false);
            // Fizik bileşeni (çarpıştırıcı boyutu, karakter yüksekliği ...) değişti: gövde/karakter yeniden kurulur
            if (info->Category == "Physics" || (info->Builtin && (info->TypeName.find("Collider") != std::string::npos || info->TypeName == "RigidbodyComponent")))
                if (PhysicsWorld3D* w = World()) w->MarkDirty(e);
            return Value::MakeBool(true);
        }

        glm::quat WorldRotation(Entity e)
        {
            // Güncel yerel dönüşten: aynı karede SetRotation'dan sonra önbellekteki WorldTransform henüz eski
            // (Teleport eski dönüşle ışınlıyordu; ör. eklemli zinciri yeni poza koyarken halkalar ayrılıyordu)
            const auto& tc = e.GetComponent<TransformComponent>();
            glm::quat local = glm::quat(tc.Rotation);
            if (Entity parent = e.GetParent(); parent && parent.HasComponent<TransformComponent>())
            {
                glm::mat4 m = parent.GetComponent<TransformComponent>().WorldTransform;
                glm::mat3 r(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])));
                return glm::normalize(glm::quat_cast(r) * local);
            }
            return glm::normalize(local);
        }
    }

    static Value Hit2DToValue(const RaycastHit2D& h)
    {
        auto* m = new CQSMapObject();
        m->Entries["entity"] = Value::MakeInt((int64_t)h.Entity);
        Entity e = EntityFromId(h.Entity);
        m->Entries["name"] = Value::MakeString(e && e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Tag : "");
        m->Entries["point"] = Vec2(h.Point);
        m->Entries["normal"] = Vec2(h.Normal);
        m->Entries["distance"] = Value::MakeFloat(h.Distance);
        m->Entries["isTrigger"] = Value::MakeBool(h.IsTrigger);
        return Value::MakeObject(m);
    }

    void RegisterPhysicsNatives(Scripting::NativeRegistry* vm)
    {
        // ── 2D sorgular ve gövde ──
        vm->DefineNative("Raycast2D", [](int argc, Value* args) -> Value {
            PhysicsWorld2D* w = World2D();
            if (!w || argc < 4) return Value::MakeNull();
            RaycastHit2D hit;
            uint32_t ignore = ScriptContext::GetCurrentEntity();
            bool ignoreSelf = argc > 6 && args[6].IsBool() && args[6].AsBool();
            if (!w->Raycast({ Num(argc, args, 0), Num(argc, args, 1) }, { Num(argc, args, 2), Num(argc, args, 3) }, Num(argc, args, 4, 1000.0f), hit,
                            Mask(argc, args, 5), ignoreSelf ? ignore : PhysicsWorld2D::NoEntity))
                return Value::MakeNull();
            return Hit2DToValue(hit);
        }, -1);
        vm->DefineNative("RaycastAll2D", [](int argc, Value* args) -> Value {
            auto* l = new CQSListObject();
            if (PhysicsWorld2D* w = World2D(); w && argc >= 4)
            {
                std::vector<RaycastHit2D> hits;
                w->RaycastAll({ Num(argc, args, 0), Num(argc, args, 1) }, { Num(argc, args, 2), Num(argc, args, 3) }, Num(argc, args, 4, 1000.0f), hits, Mask(argc, args, 5));
                for (auto& h : hits) l->Elements.push_back(Hit2DToValue(h));
            }
            return Value::MakeObject(l);
        }, -1);
        vm->DefineNative("OverlapCircle2D", [](int argc, Value* args) -> Value {
            std::vector<uint32_t> ids;
            if (PhysicsWorld2D* w = World2D(); w && argc >= 3)
                w->OverlapCircle({ Num(argc, args, 0), Num(argc, args, 1) }, Num(argc, args, 2), ids, Mask(argc, args, 3));
            return EntityList(ids);
        }, -1);
        vm->DefineNative("OverlapBox2D", [](int argc, Value* args) -> Value {
            std::vector<uint32_t> ids;
            if (PhysicsWorld2D* w = World2D(); w && argc >= 4)
                w->OverlapBox({ Num(argc, args, 0), Num(argc, args, 1) }, { Num(argc, args, 2), Num(argc, args, 3) }, glm::radians(Num(argc, args, 4)), ids, Mask(argc, args, 5));
            return EntityList(ids);
        }, -1);
        vm->DefineNative("ApplyTorque2D", [](int argc, Value* args) -> Value {
            if (PhysicsWorld2D* w = World2D(); w && argc >= 1)
                if (Entity e = Current()) w->ApplyTorque(e, Num(argc, args, 0));
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("ApplyForceAtPosition2D", [](int argc, Value* args) -> Value {
            if (PhysicsWorld2D* w = World2D(); w && argc >= 4)
                if (Entity e = Current()) w->ApplyForce(e, { Num(argc, args, 0), Num(argc, args, 1) }, { Num(argc, args, 2), Num(argc, args, 3) });
            return Value::MakeNull();
        }, 4);
        vm->DefineNative("GetMass2D", [](int, Value*) -> Value {
            PhysicsWorld2D* w = World2D();
            Entity e = Current();
            return Value::MakeFloat(w && e ? w->GetMass(e) : 0.0f);
        }, 0);
        vm->DefineNative("Teleport2D", [](int argc, Value* args) -> Value {
            PhysicsWorld2D* w = World2D();
            Entity e = Current();
            if (!w || !e || argc < 2) return Value::MakeNull();
            float angle = argc > 2 ? glm::radians(Num(argc, args, 2)) : e.GetComponent<TransformComponent>().Rotation.z;
            w->Teleport(e, { Num(argc, args, 0), Num(argc, args, 1) }, angle);
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("SetGravity2D", [](int argc, Value* args) -> Value {
            if (PhysicsWorld2D* w = World2D(); w && argc >= 2)
                w->SetGravity({ Num(argc, args, 0), Num(argc, args, 1) });
            return Value::MakeNull();
        }, 2);
        vm->DefineNative("GetGravity2D", [](int, Value*) -> Value {
            PhysicsWorld2D* w = World2D();
            return Vec2(w ? w->GetGravity() : PhysicsSettings::Get().Gravity2D);
        }, 0);

        // ── Sorgular ──
        // Raycast(..., maxDistance, layerMask, ignoreSelf): ignoreSelf betiğin kendi gövdesini (karakter kapsülü dahil) yok sayar
        vm->DefineNative("Raycast", [](int argc, Value* args) -> Value {
            PhysicsWorld3D* w = World();
            if (!w || argc < 6) return Value::MakeNull();
            Physics3D::RaycastHit hit;
            if (!w->Raycast({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, { Num(argc, args, 3), Num(argc, args, 4), Num(argc, args, 5) },
                            Num(argc, args, 6, 1000.0f), hit, Mask(argc, args, 7), IgnoreSelf(argc, args, 8)))
                return Value::MakeNull();
            return HitToValue(hit);
        }, -1);
        vm->DefineNative("RaycastAll", [](int argc, Value* args) -> Value {
            auto* l = new CQSListObject();
            PhysicsWorld3D* w = World();
            if (w && argc >= 6)
            {
                std::vector<Physics3D::RaycastHit> hits;
                w->RaycastAll({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, { Num(argc, args, 3), Num(argc, args, 4), Num(argc, args, 5) },
                              Num(argc, args, 6, 1000.0f), hits, Mask(argc, args, 7));
                for (auto& h : hits) l->Elements.push_back(HitToValue(h));
            }
            return Value::MakeObject(l);
        }, -1);
        vm->DefineNative("SphereCast", [](int argc, Value* args) -> Value {
            PhysicsWorld3D* w = World();
            if (!w || argc < 7) return Value::MakeNull();
            Physics3D::RaycastHit hit;
            if (!w->SphereCast({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, Num(argc, args, 3),
                               { Num(argc, args, 4), Num(argc, args, 5), Num(argc, args, 6) }, Num(argc, args, 7, 1000.0f), hit, Mask(argc, args, 8),
                               IgnoreSelf(argc, args, 9)))
                return Value::MakeNull();
            return HitToValue(hit);
        }, -1);
        vm->DefineNative("OverlapSphere", [](int argc, Value* args) -> Value {
            std::vector<uint32_t> ids;
            if (PhysicsWorld3D* w = World(); w && argc >= 4)
                w->OverlapSphere({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, Num(argc, args, 3), ids, Mask(argc, args, 4));
            return EntityList(ids);
        }, -1);
        vm->DefineNative("OverlapBox", [](int argc, Value* args) -> Value {
            std::vector<uint32_t> ids;
            if (PhysicsWorld3D* w = World(); w && argc >= 6)
                w->OverlapBox({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, { Num(argc, args, 3), Num(argc, args, 4), Num(argc, args, 5) },
                              glm::quat(1, 0, 0, 0), ids, Mask(argc, args, 6));
            return EntityList(ids);
        }, -1);

        // ── Gövde ──
        vm->DefineNative("AddForceAtPosition", [](int argc, Value* args) -> Value {
            if (PhysicsWorld3D* w = World(); w && argc >= 6)
                if (Entity e = Current())
                    w->AddForceAtPosition(e, { Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, { Num(argc, args, 3), Num(argc, args, 4), Num(argc, args, 5) });
            return Value::MakeNull();
        }, 6);
        vm->DefineNative("AddExplosionForce", [](int argc, Value* args) -> Value {
            if (PhysicsWorld3D* w = World(); w && argc >= 5)
                w->AddExplosionForce(Num(argc, args, 0), { Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) }, Num(argc, args, 4), Num(argc, args, 5));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("ApplyAngularImpulse", [](int argc, Value* args) -> Value {
            if (PhysicsWorld3D* w = World(); w && argc >= 3)
                if (Entity e = Current())
                    w->ApplyTorqueImpulse(e, { Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) });
            return Value::MakeNull();
        }, 3);
        vm->DefineNative("GetMass", [](int, Value*) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            return Value::MakeFloat(w && e ? w->GetMass(e) : 0.0f);
        }, 0);
        vm->DefineNative("IsSleeping", [](int, Value*) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            return Value::MakeBool(w && e && w->IsSleeping(e));
        }, 0);
        vm->DefineNative("WakeUp", [](int, Value*) -> Value {
            if (PhysicsWorld3D* w = World())
                if (Entity e = Current()) w->WakeUp(e);
            return Value::MakeNull();
        }, 0);
        vm->DefineNative("Teleport", [](int argc, Value* args) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            if (!w || !e || argc < 3) return Value::MakeNull();
            w->Teleport(e, { Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, WorldRotation(e));
            return Value::MakeNull();
        }, 3);
        vm->DefineNative("SetGravity", [](int argc, Value* args) -> Value {
            if (PhysicsWorld3D* w = World(); w && argc >= 3)
                w->SetGravity({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) });
            return Value::MakeNull();
        }, 3);
        vm->DefineNative("GetGravity", [](int, Value*) -> Value {
            PhysicsWorld3D* w = World();
            return Vec3(w ? w->GetGravity() : PhysicsSettings::Get().Gravity);
        }, 0);
        vm->DefineNative("GetPhysicsBackend", [](int, Value*) -> Value {
            PhysicsWorld3D* w = World();
            return Value::MakeString(w ? w->GetBackendName() : "None");
        }, 0);

        // ── Karakter denetleyicisi ──
        vm->DefineNative("CharacterMove", [](int argc, Value* args) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            if (!w || !e || argc < 3) return Value::MakeInt(0);
            auto st = w->MoveCharacter(e, { Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) });
            return Value::MakeInt((int64_t)st.Flags);
        }, 3);
        vm->DefineNative("CharacterSimpleMove", [](int argc, Value* args) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            if (!w || !e || argc < 3) return Value::MakeBool(false);
            auto st = w->SimpleMoveCharacter(e, { Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) });
            return Value::MakeBool(st.Grounded);
        }, 3);
        vm->DefineNative("CharacterIsGrounded", [](int, Value*) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            return Value::MakeBool(w && e && w->GetCharacterState(e).Grounded);
        }, 0);
        vm->DefineNative("CharacterGetVelocity", [](int, Value*) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            return Vec3(w && e ? w->GetCharacterState(e).Velocity : glm::vec3(0.0f));
        }, 0);
        vm->DefineNative("CharacterGetFlags", [](int, Value*) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            return Value::MakeInt(w && e ? (int64_t)w->GetCharacterState(e).Flags : 0);
        }, 0);
        vm->DefineNative("CharacterGetGroundNormal", [](int, Value*) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            return Vec3(w && e ? w->GetCharacterState(e).GroundNormal : glm::vec3(0, 1, 0));
        }, 0);
        vm->DefineNative("CharacterGetGroundEntity", [](int, Value*) -> Value {
            // Üzerinde durulan gövdenin varlığı (hareketli platformlar, zıplatma pedleri); havadayken -1
            PhysicsWorld3D* w = World();
            Entity e = Current();
            if (!w || !e)
                return Value::MakeInt(-1);
            uint32_t ground = w->GetCharacterState(e).GroundEntity;
            if (ground == Physics3D::NoEntity || !EntityFromId((int64_t)ground))
                return Value::MakeInt(-1);
            return Value::MakeInt((int64_t)ground);
        }, 0);

        // ── Temas (OnCollision* / OnTrigger* içinde) ──
        vm->DefineNative("GetContactOther", [](int, Value*) -> Value {
            auto& c = ScriptContext::GetContact();
            return c.Valid ? Value::MakeInt((int64_t)c.Other) : Value::MakeNull();
        }, 0);
        vm->DefineNative("GetContactPoint", [](int, Value*) -> Value { return Vec3(ScriptContext::GetContact().Point); }, 0);
        vm->DefineNative("GetContactNormal", [](int, Value*) -> Value { return Vec3(ScriptContext::GetContact().Normal); }, 0);
        vm->DefineNative("GetContactImpulse", [](int, Value*) -> Value { return Value::MakeFloat(ScriptContext::GetContact().Impulse); }, 0);
        vm->DefineNative("GetContactRelativeVelocity", [](int, Value*) -> Value { return Vec3(ScriptContext::GetContact().RelativeVelocity); }, 0);

        // ── Eklemler ──
        vm->DefineNative("JointSetMotor", [](int argc, Value* args) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = Current();
            if (!w || !e || argc < 1) return Value::MakeNull();
            bool enabled = args[0].IsBool() ? args[0].AsBool() : args[0].ToNumber() != 0.0;
            float target = Num(argc, args, 1), force = Num(argc, args, 2, 100.0f);
            // Bileşeni de güncelle (denetçi + yeniden kurulum tutarlı kalsın)
            if (e.HasComponent<HingeJointComponent>())
            {
                auto& j = e.GetComponent<HingeJointComponent>();
                j.UseMotor = enabled; j.MotorTargetVelocity = target; j.MotorForce = force;
            }
            if (e.HasComponent<SliderJointComponent>())
            {
                auto& j = e.GetComponent<SliderJointComponent>();
                j.UseMotor = enabled; j.MotorTargetSpeed = target; j.MotorForce = force;
            }
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("JointGetAngle", [](int, Value*) -> Value {
            Entity e = Current();
            return Value::MakeFloat(e && e.HasComponent<HingeJointComponent>() ? e.GetComponent<HingeJointComponent>().CurrentAngle : 0.0f);
        }, 0);
        vm->DefineNative("JointIsBroken", [](int, Value*) -> Value {
            Entity e = Current();
            if (!e) return Value::MakeBool(false);
            bool broken = false;
            if (e.HasComponent<FixedJointComponent>()) broken |= e.GetComponent<FixedJointComponent>().Broken;
            if (e.HasComponent<HingeJointComponent>()) broken |= e.GetComponent<HingeJointComponent>().Broken;
            if (e.HasComponent<SliderJointComponent>()) broken |= e.GetComponent<SliderJointComponent>().Broken;
            if (e.HasComponent<SpringJointComponent>()) broken |= e.GetComponent<SpringJointComponent>().Broken;
            if (e.HasComponent<CharacterJointComponent>()) broken |= e.GetComponent<CharacterJointComponent>().Broken;
            if (e.HasComponent<BallJointComponent>()) broken |= e.GetComponent<BallJointComponent>().Broken;
            if (e.HasComponent<ConfigurableJointComponent>()) broken |= e.GetComponent<ConfigurableJointComponent>().Broken;
            return Value::MakeBool(broken);
        }, 0);

        // ── Genel bileşen erişimi ──
        vm->DefineNative("HasComponent", [](int argc, Value* args) -> Value {
            Entity e = Current();
            ComponentInfo* info = argc > 0 ? ComponentRegistry::Find(args[0].ToString()) : nullptr;
            return Value::MakeBool(e && info && info->Has(e));
        }, 1);
        vm->DefineNative("AddComponent", [](int argc, Value* args) -> Value {
            Entity e = Current();
            ComponentInfo* info = argc > 0 ? ComponentRegistry::Find(args[0].ToString()) : nullptr;
            if (!e || !info) return Value::MakeBool(false);
            ComponentRegistry::AddWithRequirements(*info, e);
            return Value::MakeBool(true);
        }, 1);
        vm->DefineNative("RemoveComponent", [](int argc, Value* args) -> Value {
            Entity e = Current();
            ComponentInfo* info = argc > 0 ? ComponentRegistry::Find(args[0].ToString()) : nullptr;
            if (!e || !info || !info->Has(e) || info->TypeName == "TransformComponent") return Value::MakeBool(false);
            info->Remove(e);
            return Value::MakeBool(true);
        }, 1);
        vm->DefineNative("GetComponentField", [](int argc, Value* args) -> Value {
            return argc >= 2 ? GetField(Current(), args[0], args[1]) : Value::MakeNull();
        }, 2);
        vm->DefineNative("SetComponentField", [](int argc, Value* args) -> Value {
            return argc >= 3 ? SetField(Current(), args[0], args[1], args[2]) : Value::MakeBool(false);
        }, 3);

        // ── Varlıklar (ID ile) ──
        vm->DefineNative("GetEntityId", [](int, Value*) -> Value {
            uint32_t id = ScriptContext::GetCurrentEntity();
            return id == ScriptContext::NullEntity ? Value::MakeNull() : Value::MakeInt((int64_t)id);
        }, 0);
        vm->DefineNative("FindEntity", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1) return Value::MakeNull();
            std::string name = args[0].ToString();
            auto view = scene->m_Registry.view<TagComponent>();
            for (auto e : view)
                if (view.get<TagComponent>(e).Tag == name)
                    return Value::MakeInt((int64_t)(uint32_t)e);
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("FindEntitiesWithTag", [](int argc, Value* args) -> Value {
            auto* l = new CQSListObject();
            Scene* scene = ActiveScene();
            if (scene && argc >= 1)
            {
                std::string tag = args[0].ToString();
                auto view = scene->m_Registry.view<TagComponent>();
                for (auto e : view)
                    if (view.get<TagComponent>(e).GameTag == tag)
                        l->Elements.push_back(Value::MakeInt((int64_t)(uint32_t)e));
            }
            return Value::MakeObject(l);
        }, 1);
        vm->DefineNative("EntityIsValid", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc > 0 && args[0].IsNumber() && (bool)EntityFromValue(args[0]));
        }, 1);
        vm->DefineNative("EntityGetName", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 ? EntityFromValue(args[0]) : Entity{};
            return Value::MakeString(e ? e.GetComponent<TagComponent>().Tag : "");
        }, 1);
        // Dünya konumu: üst zincirin yerel dönüşümlerinden (bu karede taşınan üst nesneler de doğru sonuç verir)
        auto worldMatrix = [](Entity e) {
            glm::mat4 m = e.GetComponent<TransformComponent>().GetTransform();
            int guard = 0;
            for (Entity p = e.GetParent(); p && guard < 256; p = p.GetParent(), ++guard)
                m = p.GetComponent<TransformComponent>().GetTransform() * m;
            return m;
        };
        vm->DefineNative("EntityGetWorldPosition", [worldMatrix](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            return e ? Vec3(glm::vec3(worldMatrix(e)[3])) : Value::MakeNull();
        }, -1);
        // Dünya yön vektörleri (birim): ileri -Z, sağ +X, yukarı +Y
        auto worldAxis = [worldMatrix](int argc, Value* args, int column, float sign) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            if (!e) return Value::MakeNull();
            glm::vec3 axis = glm::vec3(worldMatrix(e)[column]) * sign;
            float len = glm::length(axis);
            return Vec3(len > 1e-8f ? axis / len : glm::vec3(0.0f));
        };
        vm->DefineNative("EntityGetForward", [worldAxis](int argc, Value* args) -> Value { return worldAxis(argc, args, 2, -1.0f); }, -1);
        vm->DefineNative("EntityGetRight", [worldAxis](int argc, Value* args) -> Value { return worldAxis(argc, args, 0, 1.0f); }, -1);
        vm->DefineNative("EntityGetUp", [worldAxis](int argc, Value* args) -> Value { return worldAxis(argc, args, 1, 1.0f); }, -1);
        // EntityAddImpulse(entity, ix, iy, iz [, px, py, pz]): başka bir gövdeye anlık itme (nokta verilirse dönme de)
        vm->DefineNative("EntityAddImpulse", [](int argc, Value* args) -> Value {
            PhysicsWorld3D* w = World();
            Entity e = argc >= 4 ? EntityFromValue(args[0]) : Entity{};
            if (!w || !e) return Value::MakeBool(false);
            glm::vec3 impulse{ Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) };
            if (argc >= 7) w->AddImpulseAtPosition(e, impulse, { Num(argc, args, 4), Num(argc, args, 5), Num(argc, args, 6) });
            else w->ApplyImpulse(e, impulse);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("EntitySetWorldPosition", [worldMatrix](int argc, Value* args) -> Value {
            Entity e = argc >= 4 ? EntityFromValue(args[0]) : Entity{};
            if (!e) return Value::MakeBool(false);
            glm::vec3 world{ Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) };
            Entity parent = e.GetParent();
            e.GetComponent<TransformComponent>().Position = parent ? glm::vec3(glm::inverse(worldMatrix(parent)) * glm::vec4(world, 1.0f)) : world;
            return Value::MakeBool(true);
        }, 4);
        // Oyun etiketi (Tag alanı: "Player", "Enemy" ...) başka bir varlık için
        vm->DefineNative("EntityGetTag", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            return Value::MakeString(e ? e.GetComponent<TagComponent>().GameTag : "");
        }, -1);
        vm->DefineNative("EntitySetTag", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? EntityFromValue(args[0]) : Entity{};
            if (!e) return Value::MakeBool(false);
            e.GetComponent<TagComponent>().GameTag = args[1].ToString();
            return Value::MakeBool(true);
        }, 2);
        // Görsel sınırlar (dünya AABB): {min, max, size, center} | null.
        // EntityGetBounds(entity = self, includeChildren = false): true ise etkin alt varlıkların sınırları da katılır
        // (ör. modeli çocukta duran bir kök: yerleştirilen mobilya, prefab kopyası)
        vm->DefineNative("EntityGetBounds", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            if (!e) return Value::MakeNull();
            const bool children = argc > 1 && args[1].IsTruthy();
            glm::vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
            bool any = false;
            std::vector<Entity> stack{ e };
            while (!stack.empty())
            {
                Entity cur = stack.back();
                stack.pop_back();
                if (cur != e && !cur.GetScene()->IsEntityActive(cur)) continue;
                glm::vec3 a, b;
                if (ScenePicking::GetWorldBounds(cur, a, b))
                {
                    mn = glm::min(mn, a);
                    mx = glm::max(mx, b);
                    any = true;
                }
                if (children)
                    for (Entity c : cur.GetChildren()) stack.push_back(c);
            }
            if (!any) return Value::MakeNull();
            auto* m = new CQSMapObject();
            m->Entries["min"] = Vec3(mn);
            m->Entries["max"] = Vec3(mx);
            m->Entries["size"] = Vec3(mx - mn);
            m->Entries["center"] = Vec3((mn + mx) * 0.5f);
            return Value::MakeObject(m);
        }, -1);
        // Kalıcı kimlik (sahne kaydında saklanır): metin olarak (64 bit sayı JS gibi dillerde taşmasın)
        vm->DefineNative("EntityGetUUID", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            if (!e || !e.HasComponent<IDComponent>()) return Value::MakeString("");
            return Value::MakeString(std::to_string((uint64_t)e.GetComponent<IDComponent>().ID));
        }, -1);
        vm->DefineNative("FindEntityByUUID", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1) return Value::MakeNull();
            uint64_t id = 0;
            try { id = std::stoull(args[0].ToString()); } catch (...) { return Value::MakeNull(); }
            Entity e = scene->FindEntityByUUID(id);
            return e ? Value::MakeInt((int64_t)(uint32_t)(entt::entity)e) : Value::MakeNull();
        }, 1);
        // Varlıktaki bileşenlerin kayıt adları (Inspector'daki adlar)
        vm->DefineNative("EntityGetComponentNames", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            auto* l = new CQSListObject();
            if (!e) return Value::MakeObject(l);
            for (auto& info : ComponentRegistry::All())
                if (info.Has && info.Has(e))
                    l->Elements.push_back(Value::MakeString(info.Name));
            return Value::MakeObject(l);
        }, -1);
        // 2D çizim sırası (Sorting Layer / Order in Layer)
        vm->DefineNative("EntitySetSortingOrder", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? EntityFromValue(args[0]) : Entity{};
            if (!e) return Value::MakeBool(false);
            auto& l = e.HasComponent<LayerComponent>() ? e.GetComponent<LayerComponent>() : e.AddComponent<LayerComponent>();
            l.OrderInLayer = (int)args[1].ToNumber();
            if (argc >= 3 && args[2].IsNumber()) l.Layer = (int)args[2].ToNumber();
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("EntityGetSortingOrder", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            return Value::MakeInt(e && e.HasComponent<LayerComponent>() ? e.GetComponent<LayerComponent>().OrderInLayer : 0);
        }, -1);
        vm->DefineNative("EntitySetName", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? EntityFromValue(args[0]) : Entity{};
            if (!e || !args[1].IsString()) return Value::MakeBool(false);
            e.GetComponent<TagComponent>().Tag = args[1].AsString()->Value;
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("EntityGetPosition", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 ? EntityFromValue(args[0]) : Entity{};
            return e ? Vec3(e.GetComponent<TransformComponent>().Position) : Value::MakeNull();
        }, 1);
        vm->DefineNative("EntitySetPosition", [](int argc, Value* args) -> Value {
            Entity e = argc >= 4 ? EntityFromValue(args[0]) : Entity{};
            if (e) e.GetComponent<TransformComponent>().Position = { Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) };
            return Value::MakeNull();
        }, 4);
        vm->DefineNative("EntityGetRotation", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 ? EntityFromValue(args[0]) : Entity{};
            return e ? Vec3(glm::degrees(e.GetComponent<TransformComponent>().Rotation)) : Value::MakeNull();
        }, 1);
        vm->DefineNative("EntitySetRotation", [](int argc, Value* args) -> Value {
            Entity e = argc >= 4 ? EntityFromValue(args[0]) : Entity{};
            if (e) e.GetComponent<TransformComponent>().Rotation = glm::radians(glm::vec3(Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3)));
            return Value::MakeNull();
        }, 4);
        vm->DefineNative("EntityGetScale", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 ? EntityFromValue(args[0]) : Entity{};
            return e ? Vec3(e.GetComponent<TransformComponent>().Scale) : Value::MakeNull();
        }, 1);
        vm->DefineNative("EntitySetScale", [](int argc, Value* args) -> Value {
            Entity e = argc >= 4 ? EntityFromValue(args[0]) : Entity{};
            if (e) e.GetComponent<TransformComponent>().Scale = { Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) };
            return Value::MakeNull();
        }, 4);
        vm->DefineNative("EntitySetActive", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? EntityFromValue(args[0]) : Entity{};
            if (!e) return Value::MakeNull();
            bool active = args[1].IsBool() ? args[1].AsBool() : args[1].ToNumber() != 0.0;
            if (!e.HasComponent<ActiveComponent>()) e.AddComponent<ActiveComponent>();
            e.GetComponent<ActiveComponent>().Active = active;
            if (PhysicsWorld3D* w = World()) w->MarkDirty(e);
            return Value::MakeNull();
        }, 2);
        vm->DefineNative("EntityDestroy", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 ? EntityFromValue(args[0]) : Entity{};
            if (e && ActiveScene()) ActiveScene()->DestroyEntity(e);
            return Value::MakeNull();
        }, 1);
        // Ekran -> dünya (fare ile seçme, tıklayarak hareket)
        vm->DefineNative("ScreenPointToRay", [](int argc, Value* args) -> Value {
            glm::vec3 o, d;
            if (argc < 2 || !ScreenPointToWorldRay(ActiveScene(), Num(argc, args, 0), Num(argc, args, 1), o, d)) return Value::MakeNull();
            auto* m = new CQSMapObject();
            m->Entries["origin"] = Vec3(o);
            m->Entries["direction"] = Vec3(d);
            return Value::MakeObject(m);
        }, 2);
        // Görsel sınırlarla ışın testi: fizik dünyası olmadan da (editör araçları) çalışır.
        // Kutunun çarpılan yüzünün dünya normali de döner.
        auto boundsHit = [](Entity e, const glm::vec3& o, const glm::vec3& d, float maxDistance, float& tHit, glm::vec3& normal) -> bool {
            glm::vec3 mn, mx;
            if (!ScenePicking::GetLocalBounds(e, mn, mx))
                return false;
            const glm::mat4& world = e.GetComponent<TransformComponent>().WorldTransform;
            glm::mat4 inv = glm::inverse(world);
            glm::vec3 lo = glm::vec3(inv * glm::vec4(o, 1.0f));
            glm::vec3 ld = glm::vec3(inv * glm::vec4(d, 0.0f));
            float tmin = 0.0f, tmax = 1e30f;
            int axis = -1;
            float sign = 1.0f;
            for (int i = 0; i < 3; ++i)
            {
                if (std::fabs(ld[i]) < 1e-9f)
                {
                    if (lo[i] < mn[i] || lo[i] > mx[i]) return false;
                    continue;
                }
                float t1 = (mn[i] - lo[i]) / ld[i], t2 = (mx[i] - lo[i]) / ld[i];
                float s = -1.0f;
                if (t1 > t2) { std::swap(t1, t2); s = 1.0f; }
                if (t1 > tmin) { tmin = t1; axis = i; sign = s; }
                tmax = std::min(tmax, t2);
                if (tmin > tmax) return false;
            }
            if (axis < 0) return false;   // başlangıç kutunun içinde
            // Yerel t -> dünya mesafesi (ölçek)
            glm::vec3 lp = lo + ld * tmin;
            glm::vec3 wp = glm::vec3(world * glm::vec4(lp, 1.0f));
            tHit = glm::length(wp - o);
            if (tHit > maxDistance) return false;
            glm::vec3 ln(0.0f);
            ln[axis] = sign;
            normal = glm::normalize(glm::vec3(glm::transpose(inv) * glm::vec4(ln, 0.0f)));
            return true;
        };
        auto hitValue = [](Entity e, const glm::vec3& p, const glm::vec3& n, float dist) {
            auto* m = new CQSMapObject();
            m->Entries["entity"] = Value::MakeInt((int64_t)(uint32_t)(entt::entity)e);
            m->Entries["name"] = Value::MakeString(e.GetComponent<TagComponent>().Tag);
            m->Entries["point"] = Vec3(p);
            m->Entries["normal"] = Vec3(n);
            m->Entries["distance"] = Value::MakeFloat(dist);
            return Value::MakeObject(m);
        };
        // SceneRaycast(ox, oy, oz, dx, dy, dz, maxDistance = 1000, ignoreEntity = -1) -> hit | null
        vm->DefineNative("SceneRaycast", [boundsHit, hitValue](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 6) return Value::MakeNull();
            glm::vec3 o{ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) };
            glm::vec3 d{ Num(argc, args, 3), Num(argc, args, 4), Num(argc, args, 5) };
            if (glm::dot(d, d) < 1e-12f) return Value::MakeNull();
            d = glm::normalize(d);
            const float maxDistance = Num(argc, args, 6, 1000.0f);
            const int64_t ignore = argc > 7 && args[7].IsNumber() ? (int64_t)args[7].ToNumber() : -1;
            Entity best;
            float bestT = maxDistance;
            glm::vec3 bestN(0, 1, 0);
            auto view = scene->m_Registry.view<TransformComponent>();
            for (auto h : view)
            {
                if ((int64_t)(uint32_t)h == ignore || !scene->IsEntityActive(h))
                    continue;
                Entity e{ h, scene };
                float t;
                glm::vec3 n;
                if (boundsHit(e, o, d, bestT, t, n) && t < bestT)
                {
                    best = e;
                    bestT = t;
                    bestN = n;
                }
            }
            if (!best) return Value::MakeNull();
            return hitValue(best, o + d * bestT, bestN, bestT);
        }, -1);
        // EntityRaycast(entity, ox, oy, oz, dx, dy, dz, maxDistance = 1000) -> hit | null: yalnızca o varlık ve çocukları
        vm->DefineNative("EntityRaycast", [boundsHit, hitValue](int argc, Value* args) -> Value {
            Entity root = argc >= 7 ? EntityFromValue(args[0]) : Entity{};
            if (!root) return Value::MakeNull();
            glm::vec3 o{ Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) };
            glm::vec3 d{ Num(argc, args, 4), Num(argc, args, 5), Num(argc, args, 6) };
            if (glm::dot(d, d) < 1e-12f) return Value::MakeNull();
            d = glm::normalize(d);
            float bestT = Num(argc, args, 7, 1000.0f);
            Entity best;
            glm::vec3 bestN(0, 1, 0);
            std::vector<Entity> stack{ root };
            while (!stack.empty())
            {
                Entity e = stack.back();
                stack.pop_back();
                float t;
                glm::vec3 n;
                if (boundsHit(e, o, d, bestT, t, n) && t < bestT) { best = e; bestT = t; bestN = n; }
                for (Entity c : e.GetChildren()) stack.push_back(c);
            }
            if (!best) return Value::MakeNull();
            return hitValue(best, o + d * bestT, bestN, bestT);
        }, -1);
        // WorldToScreenPoint(x, y, z) -> {x, y, u, v, depth, visible}: x/y ekran pikseli, u/v viewport içinde 0..1
        vm->DefineNative("WorldToScreenPoint", [](int argc, Value* args) -> Value {
            glm::vec2 screen{ 0.0f }, uv{ 0.0f };
            float depth = 0.0f;
            Scene* scene = ActiveScene();
            if (argc < 3 || !scene) return Value::MakeNull();
            bool front = WorldToScreenPoint(scene, { Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, screen, uv, depth);
            if (depth == 0.0f && !front) return Value::MakeNull();
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(screen.x);
            m->Entries["y"] = Value::MakeFloat(screen.y);
            m->Entries["u"] = Value::MakeFloat(uv.x);
            m->Entries["v"] = Value::MakeFloat(uv.y);
            m->Entries["depth"] = Value::MakeFloat(depth);
            m->Entries["visible"] = Value::MakeBool(front && uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f);
            return Value::MakeObject(m);
        }, 3);
        vm->DefineNative("ScreenToGroundPoint", [](int argc, Value* args) -> Value {
            // Fare ışınının y = groundY düzlemiyle kesişimi
            glm::vec3 o, d;
            if (argc < 2 || !ScreenPointToWorldRay(ActiveScene(), Num(argc, args, 0), Num(argc, args, 1), o, d)) return Value::MakeNull();
            float groundY = Num(argc, args, 2, 0.0f);
            if (std::fabs(d.y) < 1e-6f) return Value::MakeNull();
            float t = (groundY - o.y) / d.y;
            if (t < 0.0f) return Value::MakeNull();
            return Vec3(o + d * t);
        }, -1);
        vm->DefineNative("RaycastFromScreen", [](int argc, Value* args) -> Value {
            // RaycastFromScreen(mx, my, maxDistance = 1000, layerMask = -1) -> hit | null
            glm::vec3 o, d;
            PhysicsWorld3D* w = World();
            if (argc < 2 || !w || !ScreenPointToWorldRay(ActiveScene(), Num(argc, args, 0), Num(argc, args, 1), o, d)) return Value::MakeNull();
            Physics3D::RaycastHit hit;
            if (!w->Raycast(o, d, Num(argc, args, 2, 1000.0f), hit, Mask(argc, args, 3)))
                return Value::MakeNull();
            return HitToValue(hit);
        }, -1);
        // ── Eğriler (Spline Container / Animate) ──
        auto splineArgs = [](int argc, Value* args, Scene*& scene, uint32_t& id) -> bool {
            scene = ActiveScene();
            Entity e = argc > 0 && args[0].IsNumber() ? EntityFromValue(args[0]) : Current();
            id = e ? (uint32_t)(entt::entity)e : 0xFFFFFFFFu;
            return scene && e;
        };
        vm->DefineNative("SplineEvaluate", [splineArgs](int argc, Value* args) -> Value {
            Scene* scene; uint32_t id; glm::vec3 p;
            if (!splineArgs(argc, args, scene, id) || !Splines::EvaluateWorld(*scene, id, Num(argc, args, 1), p)) return Value::MakeNull();
            return Vec3(p);
        }, 2);
        vm->DefineNative("SplineTangent", [splineArgs](int argc, Value* args) -> Value {
            Scene* scene; uint32_t id; glm::vec3 p, t;
            if (!splineArgs(argc, args, scene, id) || !Splines::EvaluateWorld(*scene, id, Num(argc, args, 1), p, &t)) return Value::MakeNull();
            return Vec3(t);
        }, 2);
        vm->DefineNative("SplineLength", [splineArgs](int argc, Value* args) -> Value {
            Scene* scene; uint32_t id;
            if (!splineArgs(argc, args, scene, id)) return Value::MakeFloat(0.0);
            return Value::MakeFloat(Splines::WorldLength(*scene, id));
        }, 1);
        vm->DefineNative("SplineNearestT", [splineArgs](int argc, Value* args) -> Value {
            Scene* scene; uint32_t id; float xyz[3] = { 0, 0, 0 };
            if (!splineArgs(argc, args, scene, id)) return Value::MakeFloat(0.0);
            if (argc >= 2 && (args[1].IsMap() || args[1].IsList())) ReadVec(args[1], xyz, 3);
            else { xyz[0] = Num(argc, args, 1); xyz[1] = Num(argc, args, 2); xyz[2] = Num(argc, args, 3); }
            return Value::MakeFloat(Splines::NearestT(*scene, id, glm::vec3(xyz[0], xyz[1], xyz[2])));
        }, -1);
        auto splineAnimate = [](int argc, Value* args) -> SplineAnimateComponent* {
            Entity e = argc > 0 && args[0].IsNumber() ? EntityFromValue(args[0]) : Current();
            return e && e.HasComponent<SplineAnimateComponent>() ? &e.GetComponent<SplineAnimateComponent>() : nullptr;
        };
        vm->DefineNative("SplineAnimatePlay", [splineAnimate](int argc, Value* args) -> Value {
            if (auto* a = splineAnimate(argc, args)) { a->Started = true; a->Playing = true; }
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("SplineAnimatePause", [splineAnimate](int argc, Value* args) -> Value {
            if (auto* a = splineAnimate(argc, args)) { a->Started = true; a->Playing = false; }
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("SplineAnimateRestart", [splineAnimate](int argc, Value* args) -> Value {
            if (auto* a = splineAnimate(argc, args)) { a->Started = true; a->Playing = true; a->Direction = 1; a->NormalizedTime = std::clamp(a->StartOffset, 0.0f, 1.0f); }
            return Value::MakeNull();
        }, -1);

        // ── Kamera sarsıntısı (Impulse) ve sanal kameralar ──
        vm->DefineNative("ImpulseGenerate", [](int argc, Value* args) -> Value {
            // ImpulseGenerate(sourceEntity = self, strength = 1): Impulse Source ayarlarıyla sarsıntı
            Scene* scene = ActiveScene();
            Entity e = argc > 0 && args[0].IsNumber() ? EntityFromValue(args[0]) : Current();
            if (scene && e) CameraRig::GenerateImpulse(*scene, (uint32_t)(entt::entity)e, Num(argc, args, 1, 1.0f));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("ImpulseAt", [](int argc, Value* args) -> Value {
            // ImpulseAt(x, y, z, amplitude = 1, frequency = 12, duration = 0.4, radius = 30)
            Scene* scene = ActiveScene();
            if (scene)
                CameraRig::GenerateImpulseAt(*scene, glm::vec3(Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2)), Num(argc, args, 3, 1.0f),
                                             Num(argc, args, 4, 12.0f), Num(argc, args, 5, 0.4f), Num(argc, args, 6, 30.0f));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("CameraBrainLiveCamera", [](int argc, Value* args) -> Value {
            // Canlı sanal kamera varlığı (yoksa null)
            Scene* scene = ActiveScene();
            if (!scene) return Value::MakeNull();
            Entity e = argc > 0 && args[0].IsNumber() ? EntityFromValue(args[0]) : Current();
            if (e && e.HasComponent<CameraBrainComponent>())
                if (Entity live = scene->FindEntityByUUID(e.GetComponent<CameraBrainComponent>().ActiveCamera))
                    return Value::MakeInt((int64_t)(uint32_t)(entt::entity)live);
            for (auto h : scene->m_Registry.view<VirtualCameraComponent>())
                if (scene->m_Registry.get<VirtualCameraComponent>(h).Live)
                    return Value::MakeInt((int64_t)(uint32_t)h);
            return Value::MakeNull();
        }, -1);

        // Hiyerarşi
        vm->DefineNative("EntitySetParent", [](int argc, Value* args) -> Value {
            // EntitySetParent(child, parent | -1, keepWorldTransform = true)
            Scene* scene = ActiveScene();
            Entity child = argc > 0 ? EntityFromValue(args[0]) : Entity{};
            if (!scene || !child) return Value::MakeBool(false);
            const bool keepWorld = argc > 2 ? args[2].IsTruthy() : true;
            const bool unparent = argc < 2 || args[1].IsNull() || (args[1].IsNumber() && args[1].ToNumber() < 0);
            if (unparent)
            {
                if (!child.GetParent()) return Value::MakeBool(true);
                glm::mat4 world = child.GetComponent<TransformComponent>().WorldTransform;
                scene->RemoveEntityParent(child);
                if (keepWorld)
                {
                    auto& tc = child.GetComponent<TransformComponent>();
                    Math::DecomposeTransform(world, tc.Position, tc.Rotation, tc.Scale);
                }
                scene->UpdateWorldTransforms();
                return Value::MakeBool(true);
            }
            Entity parent = EntityFromValue(args[1]);
            if (!parent || parent == child) return Value::MakeBool(false);
            for (Entity p = parent; p; p = p.GetParent())
                if (p == child) return Value::MakeBool(false);   // döngü olmaz
            scene->SetEntityParent(child, parent, keepWorld);
            scene->UpdateWorldTransforms();
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("EntityGetParent", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            Entity p = e ? e.GetParent() : Entity{};
            return Value::MakeInt(p ? (int64_t)(uint32_t)p : -1);
        }, -1);
        vm->DefineNative("EntityGetChildren", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            auto* l = new CQSListObject();
            if (e)
                for (Entity c : e.GetChildren()) l->Elements.push_back(Value::MakeInt((int64_t)(uint32_t)c));
            return Value::MakeObject(l);
        }, -1);
        vm->DefineNative("EntityFindChild", [](int argc, Value* args) -> Value {
            // EntityFindChild(entity, name): alt ağaçta (derinlik öncelikli) ada göre
            Entity e = argc > 0 ? EntityFromValue(args[0]) : Entity{};
            if (!e || argc < 2) return Value::MakeInt(-1);
            const std::string name = args[1].ToString();
            std::function<Entity(Entity)> find = [&](Entity x) -> Entity {
                for (Entity c : x.GetChildren())
                {
                    if (c.GetComponent<TagComponent>().Tag == name) return c;
                    if (Entity r = find(c)) return r;
                }
                return {};
            };
            Entity found = find(e);
            return Value::MakeInt(found ? (int64_t)(uint32_t)found : -1);
        }, 2);
        vm->DefineNative("EntityHasComponent", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? EntityFromValue(args[0]) : Entity{};
            ComponentInfo* info = argc >= 2 ? ComponentRegistry::Find(args[1].ToString()) : nullptr;
            return Value::MakeBool(e && info && info->Has(e));
        }, 2);
        vm->DefineNative("EntityAddComponent", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? EntityFromValue(args[0]) : Entity{};
            ComponentInfo* info = argc >= 2 ? ComponentRegistry::Find(args[1].ToString()) : nullptr;
            if (!e || !info) return Value::MakeBool(false);
            ComponentRegistry::AddWithRequirements(*info, e);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("EntityGetComponentField", [](int argc, Value* args) -> Value {
            return argc >= 3 ? GetField(EntityFromValue(args[0]), args[1], args[2]) : Value::MakeNull();
        }, 3);
        vm->DefineNative("EntitySetComponentField", [](int argc, Value* args) -> Value {
            return argc >= 4 ? SetField(EntityFromValue(args[0]), args[1], args[2], args[3]) : Value::MakeBool(false);
        }, 4);
        // Instantiate(entityId | "Assets/X.cqprefab", x, y, z, rx, ry, rz): kök dünyada (ebeveynsiz) oluşturulur
        vm->DefineNative("Instantiate", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1) return Value::MakeNull();
            Entity copy;
            if (args[0].IsString())
            {
                copy = PrefabSerializer::Instantiate(*scene, args[0].AsString()->Value);
            }
            else
            {
                Entity src = EntityFromValue(args[0]);
                if (!src) return Value::MakeNull();
                std::unordered_map<uint64_t, uint64_t> uuidMap;
                copy = scene->DuplicateEntityTree(src, src.GetComponent<TagComponent>().Tag, Entity{}, &uuidMap);
                scene->RemapEntityReferences(copy, uuidMap);
                // Çocuk bir kaynak kökte yaratılır: dünya konumunu koru
                if (src.GetParent())
                {
                    auto& tc = copy.GetComponent<TransformComponent>();
                    Math::DecomposeTransform(src.GetComponent<TransformComponent>().WorldTransform, tc.Position, tc.Rotation, tc.Scale);
                }
                scene->OnEntityTreeSpawned(copy);
            }
            if (!copy) return Value::MakeNull();
            auto& tc = copy.GetComponent<TransformComponent>();
            if (argc >= 4)
                tc.Position = { Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) };
            if (argc >= 7)
                tc.Rotation = glm::radians(glm::vec3(Num(argc, args, 4), Num(argc, args, 5), Num(argc, args, 6)));
            scene->UpdateWorldTransforms();
            return Value::MakeInt((int64_t)(uint32_t)copy);
        }, -1);
        vm->DefineNative("PrefabExists", [](int argc, Value* args) -> Value {
            if (argc < 1 || !args[0].IsString()) return Value::MakeBool(false);
            std::error_code ec;
            return Value::MakeBool(std::filesystem::exists(PrefabSerializer::Resolve(args[0].AsString()->Value), ec));
        }, 1);
        vm->DefineNative("GetPrefabPath", [](int argc, Value* args) -> Value {
            Entity e = argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0 ? EntityFromValue(args[0]) : Current();
            return Value::MakeString(e ? PrefabSerializer::GetPrefabPath(e) : std::string());
        }, -1);
    }
}
