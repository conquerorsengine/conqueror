#include "Scene/MeshRendererMaterial.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "CQSNativeBridge.h"
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/ScriptRuntime.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "UI/UISystem.h"
#include "Scene/PlayerInput.h"
#include "Scene/RigConstraints.h"
#include "AISystem/Navigation/NavAgents.h"
#include "AISystem/Navigation/NavMesh.h"
#include "Scene/VideoPlayer.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Particles/ParticleSystem.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Utils/PlayerPrefs.h"
#include "Core/Utils/Platform.h"
#include "Core/Input/InputGlyphs.h"
#include "Core/Project/Project.h"
#include "Core/Audio/AudioOneShots.h"
#include "Core/Audio/AudioSource.h"
#include "Core/Audio/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

// Oyun arayüzü (UI canvas öğeleri), parçacık sistemleri, betikler arası mesajlaşma ve kalıcı oyuncu tercihleri (PlayerPrefs).
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

        Entity FindByName(const std::string& name)
        {
            Scene* scene = ActiveScene();
            if (!scene || name.empty())
                return {};
            auto view = scene->m_Registry.view<TagComponent>();
            for (auto e : view)
                if (view.get<TagComponent>(e).Tag == name)
                    return Entity{ e, scene };
            return {};
        }

        Entity Target(const Value& v)
        {
            if (v.IsNumber())
            {
                const double id = v.ToNumber();
                return id < 0 ? Current() : EntityFromId((int64_t)id);
            }
            if (v.IsString())
                return FindByName(v.AsString()->Value);
            if (v.IsNull())
                return Current();
            return {};
        }

        Entity TargetArg(int argc, Value* args, int i = 0) { return i < argc ? Target(args[i]) : Current(); }

        float Num(int argc, Value* args, int i, float def = 0.0f)
        {
            return i < argc && args[i].IsNumber() ? (float)args[i].ToNumber() : def;
        }

        bool Truthy(const Value& v) { return v.IsBool() ? v.AsBool() : v.IsNumber() ? v.ToNumber() != 0.0 : !v.IsNull(); }

        std::string Text(const Value& v) { return v.IsString() ? v.AsString()->Value : v.IsNull() ? std::string() : v.ToString(); }

        // (r, g, b, a = 1) ya da {r, g, b, a} / {x, y, z, w} / [r, g, b, a]
        glm::vec4 ReadColor(int argc, Value* args, int i)
        {
            glm::vec4 c{ 1.0f };
            if (i < argc && args[i].IsMap())
            {
                auto& m = args[i].AsMap()->Entries;
                const char* rgba[4] = { "r", "g", "b", "a" };
                const char* xyzw[4] = { "x", "y", "z", "w" };
                for (int k = 0; k < 4; ++k)
                {
                    auto it = m.find(rgba[k]);
                    if (it == m.end()) it = m.find(xyzw[k]);
                    if (it != m.end()) c[k] = (float)it->second.ToNumber();
                }
                return c;
            }
            if (i < argc && args[i].IsList())
            {
                auto& l = args[i].AsList()->Elements;
                for (int k = 0; k < 4 && k < (int)l.size(); ++k)
                    c[k] = (float)l[(size_t)k].ToNumber();
                return c;
            }
            return { Num(argc, args, i, 1.0f), Num(argc, args, i + 1, 1.0f), Num(argc, args, i + 2, 1.0f), Num(argc, args, i + 3, 1.0f) };
        }

        Value Vec2(const glm::vec2& v)
        {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(v.x);
            m->Entries["y"] = Value::MakeFloat(v.y);
            return Value::MakeObject(m);
        }

        Value Vec3(const glm::vec3& v)
        {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(v.x);
            m->Entries["y"] = Value::MakeFloat(v.y);
            m->Entries["z"] = Value::MakeFloat(v.z);
            return Value::MakeObject(m);
        }

        // Kendisi ya da (buton etiketi gibi) bir alt öğe
        template<typename T>
        T* SelfOrChild(Entity e, int depth = 0)
        {
            if (!e)
                return nullptr;
            if (e.HasComponent<T>())
                return &e.GetComponent<T>();
            if (depth >= 4)
                return nullptr;
            for (Entity c : e.GetChildren())
                if (T* t = SelfOrChild<T>(c, depth + 1))
                    return t;
            return nullptr;
        }

        template<typename T>
        T* Get(Entity e) { return e && e.HasComponent<T>() ? &e.GetComponent<T>() : nullptr; }

        UISelectableState* Selectable(Entity e)
        {
            if (auto* b = Get<UIButtonComponent>(e)) return &b->Selectable;
            if (auto* t = Get<UIToggleComponent>(e)) return &t->Selectable;
            if (auto* s = Get<UISliderComponent>(e)) return &s->Selectable;
            if (auto* f = Get<UIInputFieldComponent>(e)) return &f->Selectable;
            if (auto* d = Get<UIDropdownComponent>(e)) return &d->Selectable;
            if (auto* sb = Get<UIScrollbarComponent>(e)) return &sb->Selectable;
            return nullptr;
        }

        int CodepointCount(const std::string& s)
        {
            int n = 0;
            for (unsigned char c : s)
                n += (c & 0xC0) != 0x80;
            return n;
        }

        const std::string& NameOf(Entity e)
        {
            static const std::string empty;
            return e && e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Tag : empty;
        }

        // Olay hedefle eşleşiyor mu: kimlik -> kaynak varlık, metin -> olay adı ya da kaynak adı
        bool EventMatches(const UIEvent& ev, const Value& target)
        {
            if (target.IsString())
            {
                const std::string& name = target.AsString()->Value;
                return ev.Name == name || (ev.Source && NameOf(ev.Source) == name);
            }
            Entity e = Target(target);
            return e && ev.Source && (uint32_t)ev.Source == (uint32_t)e;
        }

        // Varlığın canlı betik örnekleri (bu karede silinenler hariç)
        std::vector<std::pair<Scripting::ScriptRuntime*, void*>> ScriptsOf(Entity e)
        {
            std::vector<std::pair<Scripting::ScriptRuntime*, void*>> out;
            if (!e || !e.HasComponent<ConquerorScriptComponent>())
                return out;
            for (auto& s : e.GetComponent<ConquerorScriptComponent>().Scripts)
                if (s.Instance && s.Runtime && !Scripting::ScriptRuntimes::IsRetired(s.Instance))
                    out.emplace_back(static_cast<Scripting::ScriptRuntime*>(s.Runtime), s.Instance);
            return out;
        }

        // Henüz örneklenmemiş betik (aynı karede Instantiate / AddScript): alan bildirilmiş mi, varsayılanı ne?
        bool PendingScriptField(const ConquerorScriptData& script, const std::string& field, Scripting::ScriptValue* defaultValue)
        {
            auto* runtime = Scripting::ScriptRuntimes::ForPath(script.ScriptPath);
            if (!runtime || !runtime->IsAvailable())
                return false;
            std::string path = script.ScriptPath;
            if (std::filesystem::path(path).is_relative())
            {
                std::error_code ec;
                const auto absolute = Project::GetActiveProjectDirectory() / path;
                if (!Project::GetActiveProjectDirectory().empty() && std::filesystem::exists(absolute, ec))
                    path = absolute.string();
            }
            if (!runtime->LoadScript(path, nullptr))
                return false;
            const std::string className = script.ClassName.empty() ? runtime->GetDefaultClass(path) : script.ClassName;
            std::vector<Scripting::ScriptFieldInfo> fields;
            if (!runtime->GetFields(path, className, fields, nullptr))
                return false;
            for (const auto& f : fields)
                if (f.Name == field)
                {
                    if (defaultValue) *defaultValue = f.HasDefault ? f.Default : Scripting::ScriptValue();
                    return true;
                }
            return false;
        }

        // Yöntemi olan örneklere çağrı; argümanlar kopyalanır (iç içe çağrı VM yığınını büyütebilir)
        int SendTo(const std::vector<std::pair<Scripting::ScriptRuntime*, void*>>& targets, const std::string& method, std::vector<Value> args)
        {
            int handled = 0;
            for (auto& [runtime, instance] : targets)
            {
                if (Scripting::ScriptRuntimes::IsRetired(instance) || !runtime->HasMethod(instance, method))
                    continue;
                runtime->Invoke(instance, method, args.empty() ? nullptr : args.data(), (int)args.size());
                ++handled;
            }
            return handled;
        }

        ParticleSystemComponent* Particles(Entity e) { return Get<ParticleSystemComponent>(e); }

        const glm::mat4& WorldOf(Entity e) { return e.GetComponent<TransformComponent>().WorldTransform; }
    }

    void RegisterUINatives(Scripting::NativeRegistry* vm)
    {
        // ================================================================ UI
        vm->DefineNative("UISetText", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            Entity e = Target(args[0]);
            if (!e) return Value::MakeBool(false);
            const std::string text = Text(args[1]);
            if (auto* t = Get<UITextComponent>(e)) { t->Text = text; return Value::MakeBool(true); }
            if (auto* in = Get<UIInputFieldComponent>(e)) { in->Text = text; in->Caret = CodepointCount(text); return Value::MakeBool(true); }
            if (auto* tr = Get<TextRendererComponent>(e)) { tr->Text = text; return Value::MakeBool(true); }
            if (auto* t = SelfOrChild<UITextComponent>(e)) { t->Text = text; return Value::MakeBool(true); }
            return Value::MakeBool(false);
        }, 2);
        vm->DefineNative("UIGetText", [](int argc, Value* args) -> Value {
            Entity e = TargetArg(argc, args);
            if (auto* t = Get<UITextComponent>(e)) return Value::MakeString(t->Text);
            if (auto* in = Get<UIInputFieldComponent>(e)) return Value::MakeString(in->Text);
            if (auto* tr = Get<TextRendererComponent>(e)) return Value::MakeString(tr->Text);
            if (auto* d = Get<UIDropdownComponent>(e))
                return Value::MakeString(d->Value >= 0 && d->Value < (int)d->Options.size() ? d->Options[(size_t)d->Value] : std::string());
            if (auto* t = SelfOrChild<UITextComponent>(e)) return Value::MakeString(t->Text);
            return Value::MakeString("");
        }, -1);
        vm->DefineNative("UISetColor", [](int argc, Value* args) -> Value {
            Entity e = TargetArg(argc, args);
            if (!e || argc < 2) return Value::MakeBool(false);
            const glm::vec4 c = ReadColor(argc, args, 1);
            if (auto* img = Get<UIImageComponent>(e)) { img->Color = c; return Value::MakeBool(true); }
            if (auto* t = Get<UITextComponent>(e)) { t->Color = c; return Value::MakeBool(true); }
            if (auto* sr = Get<SpriteRendererComponent>(e)) { sr->Color = c; return Value::MakeBool(true); }
            if (auto* tr = Get<TextRendererComponent>(e)) { tr->Color = c; return Value::MakeBool(true); }
            return Value::MakeBool(false);
        }, -1);
        vm->DefineNative("UISetTextColor", [](int argc, Value* args) -> Value {
            auto* t = argc >= 2 ? SelfOrChild<UITextComponent>(Target(args[0])) : nullptr;
            if (!t) return Value::MakeBool(false);
            t->Color = ReadColor(argc, args, 1);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("UISetFontSize", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            const float size = std::max(1.0f, Num(argc, args, 1, 32.0f));
            if (auto* in = Get<UIInputFieldComponent>(e)) { in->FontSize = size; return Value::MakeBool(true); }
            if (auto* t = SelfOrChild<UITextComponent>(e)) { t->FontSize = size; return Value::MakeBool(true); }
            return Value::MakeBool(false);
        }, 2);
        vm->DefineNative("UISetVisible", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            if (!e) return Value::MakeBool(false);
            if (!e.HasComponent<ActiveComponent>()) e.AddComponent<ActiveComponent>();
            e.GetComponent<ActiveComponent>().Active = Truthy(args[1]);
            if (Scene* scene = ActiveScene())
                if (PhysicsWorld3D* w = scene->GetPhysicsWorld3D())
                    w->MarkDirty(e);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("UIIsVisible", [](int argc, Value* args) -> Value {
            Entity e = TargetArg(argc, args);
            Scene* scene = ActiveScene();
            return Value::MakeBool(e && scene && scene->IsEntityActive((entt::entity)e));
        }, -1);
        vm->DefineNative("UISetFill", [](int argc, Value* args) -> Value {
            auto* img = argc >= 2 ? Get<UIImageComponent>(Target(args[0])) : nullptr;
            if (!img) return Value::MakeBool(false);
            img->FillAmount = std::clamp(Num(argc, args, 1, 1.0f), 0.0f, 1.0f);
            if (img->Type == UIImageType::Simple)
                img->Type = UIImageType::Filled;
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("UIGetFill", [](int argc, Value* args) -> Value {
            auto* img = Get<UIImageComponent>(TargetArg(argc, args));
            return Value::MakeFloat(img ? img->FillAmount : 0.0f);
        }, -1);
        vm->DefineNative("UISetImage", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            const std::string path = argc >= 2 ? Text(args[1]) : std::string();
            const std::string resolved = path.empty() ? std::string() : SceneSerializer::ResolveAssetPath(path);
            if (auto* img = Get<UIImageComponent>(e)) { img->TexturePath = resolved; return Value::MakeBool(true); }
            if (auto* raw = Get<UIRawImageComponent>(e)) { raw->TexturePath = resolved; return Value::MakeBool(true); }
            return Value::MakeBool(false);
        }, 2);
        // UISetUVRect(target, x, y, w, h): Raw Image'ın doku dikdörtgeni (atlas / kaydırmalı arka plan)
        vm->DefineNative("UISetUVRect", [](int argc, Value* args) -> Value {
            auto* raw = argc >= 5 ? Get<UIRawImageComponent>(Target(args[0])) : nullptr;
            if (!raw) return Value::MakeBool(false);
            raw->UVRect = { Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3, 1.0f), Num(argc, args, 4, 1.0f) };
            return Value::MakeBool(true);
        }, 5);
        // UISetAlpha(target, alpha): Canvas Group saydamlığı (yoksa eklenir) — tüm alt ağaç solar
        vm->DefineNative("UISetAlpha", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            if (!e || !e.HasComponent<RectTransformComponent>()) return Value::MakeBool(false);
            auto& group = e.HasComponent<UICanvasGroupComponent>() ? e.GetComponent<UICanvasGroupComponent>() : e.AddComponent<UICanvasGroupComponent>();
            group.Alpha = std::clamp(Num(argc, args, 1, 1.0f), 0.0f, 1.0f);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("UIGetAlpha", [](int argc, Value* args) -> Value {
            auto* group = Get<UICanvasGroupComponent>(TargetArg(argc, args));
            return Value::MakeFloat(group ? group->Alpha : 1.0f);
        }, -1);
        // UISetGroupInteractable(target, on): Canvas Group ile tüm alt ağacın etkileşimini aç / kapat
        vm->DefineNative("UISetGroupInteractable", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            if (!e || !e.HasComponent<RectTransformComponent>()) return Value::MakeBool(false);
            auto& group = e.HasComponent<UICanvasGroupComponent>() ? e.GetComponent<UICanvasGroupComponent>() : e.AddComponent<UICanvasGroupComponent>();
            group.Interactable = Truthy(args[1]);
            group.BlocksRaycasts = group.Interactable || group.BlocksRaycasts;
            return Value::MakeBool(true);
        }, 2);
        // Klavye / gamepad gezinmesinde seçili öğe
        vm->DefineNative("UISetSelected", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene) return Value::MakeBool(false);
            Entity e = argc >= 1 && !args[0].IsNull() ? Target(args[0]) : Entity{};
            UISystem::SetSelected(*scene, e);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("UIGetSelected", [](int, Value*) -> Value {
            Scene* scene = ActiveScene();
            Entity e = scene ? UISystem::GetSelected(*scene) : Entity{};
            return e ? Value::MakeInt((int64_t)(uint32_t)e) : Value::MakeInt(-1);
        }, 0);
        vm->DefineNative("UISetValue", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            if (auto* s = Get<UISliderComponent>(e))
            {
                float lo = std::min(s->MinValue, s->MaxValue), hi = std::max(s->MinValue, s->MaxValue);
                float v = std::clamp(Num(argc, args, 1, s->Value), lo, hi);
                s->Value = s->WholeNumbers ? std::round(v) : v;
                return Value::MakeBool(true);
            }
            if (auto* t = Get<UIToggleComponent>(e)) { t->IsOn = Truthy(args[1]); return Value::MakeBool(true); }
            if (auto* d = Get<UIDropdownComponent>(e))
            {
                d->Value = d->Options.empty() ? 0 : std::clamp((int)Num(argc, args, 1, 0.0f), 0, (int)d->Options.size() - 1);
                return Value::MakeBool(true);
            }
            if (auto* sc = Get<UIScrollRectComponent>(e)) { sc->NormalizedPosition.y = std::clamp(Num(argc, args, 1, 1.0f), 0.0f, 1.0f); return Value::MakeBool(true); }
            if (auto* sb = Get<UIScrollbarComponent>(e)) { sb->Value = std::clamp(Num(argc, args, 1, 0.0f), 0.0f, 1.0f); return Value::MakeBool(true); }
            return Value::MakeBool(false);
        }, 2);
        vm->DefineNative("UIGetValue", [](int argc, Value* args) -> Value {
            Entity e = TargetArg(argc, args);
            if (auto* s = Get<UISliderComponent>(e)) return Value::MakeFloat(s->Value);
            if (auto* t = Get<UIToggleComponent>(e)) return Value::MakeBool(t->IsOn);
            if (auto* d = Get<UIDropdownComponent>(e)) return Value::MakeInt(d->Value);
            if (auto* sc = Get<UIScrollRectComponent>(e)) return Value::MakeFloat(sc->NormalizedPosition.y);
            if (auto* sb = Get<UIScrollbarComponent>(e)) return Value::MakeFloat(sb->Value);
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("UISetDropdownOptions", [](int argc, Value* args) -> Value {
            auto* d = argc >= 2 ? Get<UIDropdownComponent>(Target(args[0])) : nullptr;
            if (!d || !args[1].IsList()) return Value::MakeBool(false);
            d->Options.clear();
            for (const Value& v : args[1].AsList()->Elements)
                d->Options.push_back(Text(v));
            d->Value = d->Options.empty() ? 0 : std::clamp(d->Value, 0, (int)d->Options.size() - 1);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("UISetInteractable", [](int argc, Value* args) -> Value {
            UISelectableState* s = argc >= 2 ? Selectable(Target(args[0])) : nullptr;
            if (!s) return Value::MakeBool(false);
            s->Interactable = Truthy(args[1]);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("UIIsInteractable", [](int argc, Value* args) -> Value {
            UISelectableState* s = Selectable(TargetArg(argc, args));
            return Value::MakeBool(s && s->Interactable);
        }, -1);
        vm->DefineNative("UIIsHovered", [](int argc, Value* args) -> Value {
            UISelectableState* s = Selectable(TargetArg(argc, args));
            return Value::MakeBool(s && s->Hovered);
        }, -1);
        vm->DefineNative("UISetPosition", [](int argc, Value* args) -> Value {
            auto* rt = argc >= 3 ? Get<RectTransformComponent>(Target(args[0])) : nullptr;
            if (!rt) return Value::MakeBool(false);
            rt->AnchoredPosition = { Num(argc, args, 1), Num(argc, args, 2) };
            return Value::MakeBool(true);
        }, 3);
        vm->DefineNative("UIGetPosition", [](int argc, Value* args) -> Value {
            auto* rt = Get<RectTransformComponent>(TargetArg(argc, args));
            return rt ? Vec2(rt->AnchoredPosition) : Value::MakeNull();
        }, -1);
        vm->DefineNative("UISetSize", [](int argc, Value* args) -> Value {
            auto* rt = argc >= 3 ? Get<RectTransformComponent>(Target(args[0])) : nullptr;
            if (!rt) return Value::MakeBool(false);
            rt->SizeDelta = { Num(argc, args, 1), Num(argc, args, 2) };
            return Value::MakeBool(true);
        }, 3);
        vm->DefineNative("UIGetSize", [](int argc, Value* args) -> Value {
            auto* rt = Get<RectTransformComponent>(TargetArg(argc, args));
            if (!rt) return Value::MakeNull();
            // Hesaplanmış dikdörtgen (çapalarla esneyen öğeler için gerçek boyut); henüz yoksa SizeDelta
            glm::vec2 size(rt->ComputedRect.z, rt->ComputedRect.w);
            return Vec2(size.x > 0.0f || size.y > 0.0f ? size : rt->SizeDelta);
        }, -1);
        vm->DefineNative("UISetScale", [](int argc, Value* args) -> Value {
            auto* rt = argc >= 2 ? Get<RectTransformComponent>(Target(args[0])) : nullptr;
            if (!rt) return Value::MakeBool(false);
            const float sx = Num(argc, args, 1, 1.0f);
            rt->Scale = { sx, Num(argc, args, 2, sx) };
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("UISetRotation", [](int argc, Value* args) -> Value {
            auto* rt = argc >= 2 ? Get<RectTransformComponent>(Target(args[0])) : nullptr;
            if (!rt) return Value::MakeBool(false);
            rt->Rotation = Num(argc, args, 1);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("UIWasClicked", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeBool(false);
            for (const UIEvent& ev : UISystem::GetEvents())
                if (ev.Type == "Click" && EventMatches(ev, args[0]))
                    return Value::MakeBool(true);
            return Value::MakeBool(false);
        }, 1);
        vm->DefineNative("UIValueChanged", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeBool(false);
            for (const UIEvent& ev : UISystem::GetEvents())
                if ((ev.Type == "ValueChanged" || ev.Type == "Submit") && EventMatches(ev, args[0]))
                    return Value::MakeBool(true);
            return Value::MakeBool(false);
        }, 1);
        vm->DefineNative("UIGetEvents", [](int, Value*) -> Value {
            auto* list = new CQSListObject();
            for (const UIEvent& ev : UISystem::GetEvents())
            {
                auto* m = new CQSMapObject();
                m->Entries["name"] = Value::MakeString(ev.Name);
                m->Entries["type"] = Value::MakeString(ev.Type);
                m->Entries["entity"] = ev.Source ? Value::MakeInt((int64_t)(uint32_t)ev.Source) : Value::MakeInt(-1);
                m->Entries["value"] = Value::MakeFloat(ev.Value);
                list->Elements.push_back(Value::MakeObject(m));
            }
            return Value::MakeObject(list);
        }, 0);
        vm->DefineNative("UIIsPointerOver", [](int, Value*) -> Value {
            return Value::MakeBool(UISystem::IsPointerOverUI());
        }, 0);
        vm->DefineNative("UICreateCanvas", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene) return Value::MakeNull();
            Entity c = UISystem::CreateCanvas(*scene, argc > 0 && args[0].IsString() ? args[0].AsString()->Value : std::string("Canvas"));
            return Value::MakeInt((int64_t)(uint32_t)c);
        }, -1);
        // UICreateElement(type, parent = ilk canvas, name = "") -> id; type: Panel, Image, RawImage, Text, Button, Toggle, Slider, Scrollbar, InputField, Dropdown, ScrollView, LayoutGroup
        vm->DefineNative("UICreateElement", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1 || !args[0].IsString()) return Value::MakeNull();
            Entity parent = argc > 1 && !args[1].IsNull() ? Target(args[1]) : Entity{};
            Entity e = UISystem::CreateElement(*scene, args[0].AsString()->Value, parent);
            if (!e) return Value::MakeNull();
            if (argc > 2 && args[2].IsString() && !args[2].AsString()->Value.empty())
                e.GetComponent<TagComponent>().Tag = args[2].AsString()->Value;
            return Value::MakeInt((int64_t)(uint32_t)e);
        }, -1);

        // ================================================================ Player Input (Unity Input System)
        // PlayerInputValue(target, "Jump") -> float, PlayerInputPressed / Released(target, "Jump") -> bool (bu kare),
        // PlayerInputMove(target) -> {x, y}, PlayerInputLook(target) -> {x, y}, PlayerInputIndex(target) -> gamepad
        // Tek argüman: eylem adı (betiğin kendi varlığı); iki argüman: (hedef, eylem)
        auto playerArgs = [](int argc, Value* args, std::string& action) -> PlayerInputComponent* {
            if (argc <= 0) return nullptr;
            action = Text(args[argc >= 2 ? 1 : 0]);
            return Get<PlayerInputComponent>(argc >= 2 ? Target(args[0]) : Current());
        };
        vm->DefineNative("PlayerInputValue", [playerArgs](int argc, Value* args) -> Value {
            std::string action;
            auto* p = playerArgs(argc, args, action);
            return Value::MakeFloat(p ? PlayerInputs::GetValue(*p, action) : 0.0f);
        }, -1);
        vm->DefineNative("PlayerInputPressed", [playerArgs](int argc, Value* args) -> Value {
            std::string action;
            auto* p = playerArgs(argc, args, action);
            return Value::MakeBool(p && PlayerInputs::WasPressed(*p, action));
        }, -1);
        vm->DefineNative("PlayerInputReleased", [playerArgs](int argc, Value* args) -> Value {
            std::string action;
            auto* p = playerArgs(argc, args, action);
            return Value::MakeBool(p && PlayerInputs::WasReleased(*p, action));
        }, -1);
        vm->DefineNative("PlayerInputMove", [](int argc, Value* args) -> Value {
            auto* p = Get<PlayerInputComponent>(TargetArg(argc, args));
            return Vec2(p ? p->Move : glm::vec2(0.0f));
        }, -1);
        vm->DefineNative("PlayerInputLook", [](int argc, Value* args) -> Value {
            auto* p = Get<PlayerInputComponent>(TargetArg(argc, args));
            return Vec2(p ? p->Look : glm::vec2(0.0f));
        }, -1);
        vm->DefineNative("PlayerInputIndex", [](int argc, Value* args) -> Value {
            auto* p = Get<PlayerInputComponent>(TargetArg(argc, args));
            return Value::MakeInt(p ? p->PlayerIndex : -1);
        }, -1);
        // ================================================================ Cloth
        // ClothGetBounds([target]) -> {minX, minY, minZ, maxX, maxY, maxZ}: kumaşın dünya uzayı sınırları
        vm->DefineNative("ClothGetBounds", [](int argc, Value* args) -> Value {
            Entity e = TargetArg(argc, args);
            auto* mr = Get<MeshRendererComponent>(e);
            if (!mr || !mr->CustomMesh) return Value::MakeNull();
            const glm::mat4& w = e.GetComponent<TransformComponent>().WorldTransform;
            glm::vec3 mn(1e30f), mx(-1e30f);
            for (const auto& v : mr->CustomMesh->GetVertices())
            {
                glm::vec3 p = glm::vec3(w * glm::vec4(v.Position, 1.0f));
                mn = glm::min(mn, p);
                mx = glm::max(mx, p);
            }
            auto* m = new CQSMapObject();
            m->Entries["minX"] = Value::MakeFloat(mn.x); m->Entries["minY"] = Value::MakeFloat(mn.y); m->Entries["minZ"] = Value::MakeFloat(mn.z);
            m->Entries["maxX"] = Value::MakeFloat(mx.x); m->Entries["maxY"] = Value::MakeFloat(mx.y); m->Entries["maxZ"] = Value::MakeFloat(mx.z);
            return Value::MakeObject(m);
        }, -1);

        // ================================================================ Video Player
        vm->DefineNative("VideoPlay", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene(); Entity e = TargetArg(argc, args);
            if (scene && e) VideoPlayers::Play(*scene, (uint32_t)e);
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("VideoPause", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene(); Entity e = TargetArg(argc, args);
            if (scene && e) VideoPlayers::Pause(*scene, (uint32_t)e);
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("VideoStop", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene(); Entity e = TargetArg(argc, args);
            if (scene && e) VideoPlayers::Stop(*scene, (uint32_t)e);
            return Value::MakeNull();
        }, -1);
        // VideoSeek([target,] seconds)
        vm->DefineNative("VideoSeek", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = argc >= 2 ? Target(args[0]) : Current();
            if (scene && e && argc >= 1) VideoPlayers::Seek(*scene, (uint32_t)e, Num(argc, args, argc >= 2 ? 1 : 0));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("VideoIsPlaying", [](int argc, Value* args) -> Value {
            auto* v = Get<VideoPlayerComponent>(TargetArg(argc, args));
            return Value::MakeBool(v && v->IsPlaying);
        }, -1);
        vm->DefineNative("VideoGetTime", [](int argc, Value* args) -> Value {
            auto* v = Get<VideoPlayerComponent>(TargetArg(argc, args));
            return Value::MakeFloat(v ? v->Time : 0.0f);
        }, -1);
        vm->DefineNative("VideoGetLength", [](int argc, Value* args) -> Value {
            auto* v = Get<VideoPlayerComponent>(TargetArg(argc, args));
            return Value::MakeFloat(v ? v->Length : 0.0f);
        }, -1);
        vm->DefineNative("VideoGetFrame", [](int argc, Value* args) -> Value {
            auto* v = Get<VideoPlayerComponent>(TargetArg(argc, args));
            return Value::MakeInt(v ? v->Frame : -1);
        }, -1);

        // ================================================================ Gezinme (NavMesh) — hedefli sürümler
        vm->DefineNative("NavAgentSetDestination", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = argc >= 4 ? Target(args[0]) : Entity{};
            if (!scene || !e) return Value::MakeBool(false);
            return Value::MakeBool(NavAgents::SetDestination(*scene, (uint32_t)e, { Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) }));
        }, 4);
        vm->DefineNative("NavResume", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = TargetArg(argc, args);
            if (scene && e) NavAgents::Stop(*scene, (uint32_t)e, false);
            return Value::MakeNull();
        }, -1);
        // NavWarp([target,] x, y, z): ajanı ağ üzerinde bir noktaya ışınla
        vm->DefineNative("NavWarp", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            const int o = argc >= 4 ? 1 : 0;
            Entity e = argc >= 4 ? Target(args[0]) : Current();
            if (!scene || !e || argc < 3) return Value::MakeBool(false);
            NavAgents::Warp(*scene, (uint32_t)e, { Num(argc, args, o), Num(argc, args, o + 1), Num(argc, args, o + 2) });
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("NavRemainingDistance", [](int argc, Value* args) -> Value {
            auto* a = Get<NavMeshAgentComponent>(TargetArg(argc, args));
            return Value::MakeFloat(a ? a->RemainingDistance : 0.0f);
        }, -1);
        vm->DefineNative("NavIsOnNavMesh", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = TargetArg(argc, args);
            if (!scene || !e) return Value::MakeBool(false);
            glm::vec3 p;
            return Value::MakeBool(scene->GetNavMesh().SamplePosition(glm::vec3(e.GetComponent<TransformComponent>().WorldTransform[3]), 0.5f, p));
        }, -1);
        // NavSamplePosition(x, y, z, maxDistance = 2) -> {x, y, z} | null
        vm->DefineNative("NavSamplePosition", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            glm::vec3 p;
            if (!scene || argc < 3 || !scene->GetNavMesh().SamplePosition({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, Num(argc, args, 3, 2.0f), p))
                return Value::MakeNull();
            return Vec3(p);
        }, -1);
        // NavFindPath(x0, y0, z0, x1, y1, z1) -> [{x, y, z}, ...] (boş: yol yok)
        vm->DefineNative("NavFindPath", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            auto* list = new CQSListObject();
            std::vector<NavPathPoint> path;
            if (scene && argc >= 6 && scene->GetNavMesh().FindPath({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, { Num(argc, args, 3), Num(argc, args, 4), Num(argc, args, 5) }, path))
                for (auto& p : path)
                    list->Elements.push_back(Vec3(p.Position));
            return Value::MakeObject(list);
        }, 6);
        // NavRaycast(x0, y0, z0, x1, y1, z1) -> true: engelsiz; false: Hit noktası NavRaycastHit() ile
        vm->DefineNative("NavRaycast", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 6) return Value::MakeNull();
            glm::vec3 hit;
            bool clear = scene->GetNavMesh().Raycast({ Num(argc, args, 0), Num(argc, args, 1), Num(argc, args, 2) }, { Num(argc, args, 3), Num(argc, args, 4), Num(argc, args, 5) }, hit);
            auto* m = new CQSMapObject();
            m->Entries["clear"] = Value::MakeBool(clear);
            m->Entries["x"] = Value::MakeFloat(hit.x);
            m->Entries["y"] = Value::MakeFloat(hit.y);
            m->Entries["z"] = Value::MakeFloat(hit.z);
            return Value::MakeObject(m);
        }, 6);

        // GetBonePosition(target, "LeftHand") -> {x, y, z}: iskeletli modelin kemiğinin dünya konumu (son poz, IK dahil)
        vm->DefineNative("GetBonePosition", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            glm::mat4 m;
            if (!scene || !e || !RigConstraints::GetBoneWorldTransform(*scene, (uint32_t)e, Text(args[1]), m))
                return Value::MakeNull();
            return Vec3(glm::vec3(m[3]));
        }, 2);
        // PlayerJoin(manager, device): elle oyuncu katılımı (-1 = klavye) -> yeni oyuncu id (-1 başarısız)
        vm->DefineNative("PlayerJoin", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            Entity m = argc >= 1 ? Target(args[0]) : Entity{};
            if (!scene || !m) return Value::MakeInt(-1);
            uint32_t id = PlayerInputs::JoinPlayer(*scene, (uint32_t)m, (int)Num(argc, args, 1, -1.0f));
            return Value::MakeInt(id ? (int64_t)id : -1);
        }, -1);

        // ================================================================ Renk (sprite / mesh / UI / metin)
        // EntitySetColor(target, r, g, b, a = 1): Sprite Renderer, Mesh Renderer, UI Image, UI Text ya da Text Renderer
        vm->DefineNative("EntitySetColor", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            if (!e) return Value::MakeBool(false);
            const glm::vec4 c = ReadColor(argc, args, 1);
            if (auto* sr = Get<SpriteRendererComponent>(e)) { sr->Color = c; return Value::MakeBool(true); }
            if (auto* mr = Get<MeshRendererComponent>(e)) { MeshRendererMaterial::SetColor(*mr, c); return Value::MakeBool(true); }
            if (auto* img = Get<UIImageComponent>(e)) { img->Color = c; return Value::MakeBool(true); }
            if (auto* t = Get<UITextComponent>(e)) { t->Color = c; return Value::MakeBool(true); }
            if (auto* tr = Get<TextRendererComponent>(e)) { tr->Color = c; return Value::MakeBool(true); }
            return Value::MakeBool(false);
        }, -1);
        vm->DefineNative("EntityGetColor", [](int argc, Value* args) -> Value {
            Entity e = TargetArg(argc, args);
            glm::vec4 c(1.0f);
            if (auto* sr = Get<SpriteRendererComponent>(e)) c = sr->Color;
            else if (auto* mr = Get<MeshRendererComponent>(e)) c = MeshRendererMaterial::GetColor(*mr);
            else if (auto* img = Get<UIImageComponent>(e)) c = img->Color;
            else if (auto* t = Get<UITextComponent>(e)) c = t->Color;
            else if (auto* tr = Get<TextRendererComponent>(e)) c = tr->Color;
            else return Value::MakeNull();
            auto* m = new CQSMapObject();
            m->Entries["r"] = Value::MakeFloat(c.r);
            m->Entries["g"] = Value::MakeFloat(c.g);
            m->Entries["b"] = Value::MakeFloat(c.b);
            m->Entries["a"] = Value::MakeFloat(c.a);
            return Value::MakeObject(m);
        }, -1);

        // ================================================================ Particles
        vm->DefineNative("ParticlePlay", [](int argc, Value* args) -> Value {
            auto* ps = Particles(TargetArg(argc, args));
            if (!ps) return Value::MakeBool(false);
            ParticleSystem::Play(*ps);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("ParticlePause", [](int argc, Value* args) -> Value {
            auto* ps = Particles(TargetArg(argc, args));
            if (!ps) return Value::MakeBool(false);
            ParticleSystem::Pause(*ps);
            return Value::MakeBool(true);
        }, -1);
        // ParticleStop(target = bu varlık, clear = true): clear false ise var olan parçacıklar ömrünü tamamlar
        vm->DefineNative("ParticleStop", [](int argc, Value* args) -> Value {
            Entity e;
            bool clear = true;
            if (argc > 0 && args[0].IsBool()) { e = Current(); clear = args[0].AsBool(); }
            else { e = TargetArg(argc, args); if (argc > 1) clear = Truthy(args[1]); }
            auto* ps = Particles(e);
            if (!ps) return Value::MakeBool(false);
            ParticleSystem::Stop(*ps, clear);
            ps->RT->Started = true;   // Play On Awake yeniden başlatmasın
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("ParticleRestart", [](int argc, Value* args) -> Value {
            auto* ps = Particles(TargetArg(argc, args));
            if (!ps) return Value::MakeBool(false);
            ParticleSystem::Restart(*ps);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("ParticleIsPlaying", [](int argc, Value* args) -> Value {
            auto* ps = Particles(TargetArg(argc, args));
            return Value::MakeBool(ps && ps->RT->Playing && !ps->RT->Paused);
        }, -1);
        vm->DefineNative("ParticleGetCount", [](int argc, Value* args) -> Value {
            auto* ps = Particles(TargetArg(argc, args));
            return Value::MakeInt(ps ? (int64_t)ps->RT->Particles.size() : 0);
        }, -1);
        // ParticleGetBounds(target = self) -> {min, max, center} dünya uzayında (parçacık yoksa null)
        vm->DefineNative("ParticleGetBounds", [](int argc, Value* args) -> Value {
            Entity e = TargetArg(argc, args);
            auto* ps = Particles(e);
            if (!ps || ps->RT->Particles.empty()) return Value::MakeNull();
            const bool local = ps->SimulationSpace == ParticleSpace::Local;
            glm::mat4 world = WorldOf(e);
            glm::vec3 mn(1e30f), mx(-1e30f), sum(0.0f);
            for (const auto& p : ps->RT->Particles)
            {
                glm::vec3 wp = local ? glm::vec3(world * glm::vec4(p.Position, 1.0f)) : p.Position;
                mn = glm::min(mn, wp);
                mx = glm::max(mx, wp);
                sum += wp;
            }
            auto* m = new CQSMapObject();
            m->Entries["min"] = Vec3(mn);
            m->Entries["max"] = Vec3(mx);
            m->Entries["center"] = Vec3(sum / (float)ps->RT->Particles.size());
            return Value::MakeObject(m);
        }, -1);
        // ParticleEmit(count) bu varlıktan; ParticleEmit(target, count)
        vm->DefineNative("ParticleEmit", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeBool(false);
            Entity e = argc >= 2 ? Target(args[0]) : Current();
            auto* ps = Particles(e);
            if (!ps) return Value::MakeBool(false);
            const int count = std::clamp((int)Num(argc, args, argc >= 2 ? 1 : 0, 1.0f), 0, 100000);
            ParticleSystem::Emit(*ps, WorldOf(e), count);
            return Value::MakeBool(true);
        }, -1);
        // ParticleEmitAt(target, x, y, z, count = 10): sistemin ayarlarıyla verilen dünya noktasından yay
        vm->DefineNative("ParticleEmitAt", [](int argc, Value* args) -> Value {
            Entity e = argc >= 4 ? Target(args[0]) : Entity{};
            auto* ps = Particles(e);
            if (!ps) return Value::MakeBool(false);
            glm::mat4 world = WorldOf(e);
            world[3] = glm::vec4(Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3), 1.0f);
            const int count = std::clamp((int)Num(argc, args, 4, 10.0f), 0, 100000);
            ParticleSystem::Emit(*ps, world, count);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("ParticleSetPreset", [](int argc, Value* args) -> Value {
            auto* ps = argc >= 2 ? Particles(Target(args[0])) : nullptr;
            return Value::MakeBool(ps && args[1].IsString() && ParticleSystem::ApplyPreset(*ps, args[1].AsString()->Value));
        }, 2);
        vm->DefineNative("ParticleSetColor", [](int argc, Value* args) -> Value {
            auto* ps = argc >= 2 ? Particles(Target(args[0])) : nullptr;
            if (!ps) return Value::MakeBool(false);
            ps->StartColorA = ps->StartColorB = ReadColor(argc, args, 1);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("ParticleSetRate", [](int argc, Value* args) -> Value {
            auto* ps = argc >= 2 ? Particles(Target(args[0])) : nullptr;
            if (!ps) return Value::MakeBool(false);
            ps->RateOverTime = std::max(0.0f, Num(argc, args, 1));
            return Value::MakeBool(true);
        }, 2);

        // ================================================================ Betikler arası
        // SendMessage(target, "Method", args...) -> bool: hedef varlığın tüm betiklerinde yöntemi çağırır (Unity SendMessage)
        vm->DefineNative("SendMessage", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            Entity e = Target(args[0]);
            const std::string method = Text(args[1]);
            return Value::MakeBool(SendTo(ScriptsOf(e), method, std::vector<Value>(args + 2, args + argc)) > 0);
        }, -1);
        // InvokeMethod(target, "Method", args...) -> değer: yöntemi bulunduran ilk betikte çağırır ve dönüş değerini verir
        // (davranış ağacı / durum makinesi gibi adı çalışma anında belli olan çağrılar; yöntem yoksa null)
        vm->DefineNative("InvokeMethod", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeNull();
            Entity e = Target(args[0]);
            const std::string method = Text(args[1]);
            for (auto& [runtime, instance] : ScriptsOf(e))
            {
                if (Scripting::ScriptRuntimes::IsRetired(instance) || !runtime->HasMethod(instance, method))
                    continue;
                Value result = Value::MakeNull();
                runtime->InvokeWithResult(instance, method, argc > 2 ? args + 2 : nullptr, argc - 2, &result);
                return result;
            }
            return Value::MakeNull();
        }, -1);
        // BroadcastMessage("Method", args...) -> int: sahnedeki tüm etkin betiklere (işleyen sayısı)
        vm->DefineNative("BroadcastMessage", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1) return Value::MakeInt(0);
            std::vector<std::pair<Scripting::ScriptRuntime*, void*>> targets;
            auto view = scene->m_Registry.view<ConquerorScriptComponent>();
            for (auto entity : view)
            {
                if (!scene->IsEntityActive(entity))
                    continue;
                for (auto& s : view.get<ConquerorScriptComponent>(entity).Scripts)
                    if (s.Instance && s.Runtime && !Scripting::ScriptRuntimes::IsRetired(s.Instance))
                        targets.emplace_back(static_cast<Scripting::ScriptRuntime*>(s.Runtime), s.Instance);
            }
            return Value::MakeInt(SendTo(targets, Text(args[0]), std::vector<Value>(args + 1, args + argc)));
        }, -1);
        // GetScriptField(target, "field") -> değer (sayı/bool/metin; yoksa null)
        vm->DefineNative("GetScriptField", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeNull();
            Entity e = Target(args[0]);
            const std::string field = Text(args[1]);
            for (auto& [runtime, instance] : ScriptsOf(e))
            {
                Scripting::ScriptValue v;
                if (runtime->GetField(instance, field, v))
                    return Scripting::ScriptRuntimes::ToValue(v);
            }
            // Henüz başlamamış betik: ayarlanmış değer ya da bildirilen varsayılan
            if (e && e.HasComponent<ConquerorScriptComponent>())
                for (auto& script : e.GetComponent<ConquerorScriptComponent>().Scripts)
                {
                    if (script.Instance || script.bLoaded)
                        continue;
                    auto it = script.FieldOverrides.find(field);
                    if (it != script.FieldOverrides.end())
                        return Scripting::ScriptRuntimes::ToValue(it->second);
                    Scripting::ScriptValue def;
                    if (PendingScriptField(script, field, &def))
                        return Scripting::ScriptRuntimes::ToValue(def);
                }
            return Value::MakeNull();
        }, 2);
        // SetScriptField(target, "field", value) -> bool
        vm->DefineNative("SetScriptField", [](int argc, Value* args) -> Value {
            if (argc < 3) return Value::MakeBool(false);
            const std::string field = Text(args[1]);
            bool representable = false;
            const Scripting::ScriptValue v = Scripting::ScriptRuntimes::FromValue(args[2], &representable);
            if (!representable) return Value::MakeBool(false);
            Entity e = Target(args[0]);
            // Önce alanı gerçekten taşıyan betik: varlıkta birden çok betik varken ilk betiğe yeni (dinamik) alan
            // eklenmesin (ör. Player'da Health + FirstPersonShooter: "yaw" Health'e yazılıyor, FPS hiç görmüyordu)
            for (auto& [runtime, instance] : ScriptsOf(e))
            {
                Scripting::ScriptValue current;
                if (runtime->GetField(instance, field, current) && runtime->SetField(instance, field, v))
                    return Value::MakeBool(true);
            }
            // Hiçbiri taşımıyorsa eski davranış: ilk betik alanı kabul eder (dinamik alan)
            for (auto& [runtime, instance] : ScriptsOf(e))
                if (runtime->SetField(instance, field, v))
                    return Value::MakeBool(true);
            // Henüz başlamamış betik (aynı karede Instantiate / AddScript): Awake'ten önce uygulanır
            if (e && e.HasComponent<ConquerorScriptComponent>())
                for (auto& script : e.GetComponent<ConquerorScriptComponent>().Scripts)
                    if (!script.Instance && !script.bLoaded && PendingScriptField(script, field, nullptr))
                    {
                        script.FieldOverrides[field] = v;
                        return Value::MakeBool(true);
                    }
            return Value::MakeBool(false);
        }, 3);
        vm->DefineNative("HasScript", [](int argc, Value* args) -> Value {
            Entity e = TargetArg(argc, args);
            if (!e || !e.HasComponent<ConquerorScriptComponent>()) return Value::MakeBool(false);
            if (argc < 2) return Value::MakeBool(!e.GetComponent<ConquerorScriptComponent>().Scripts.empty());
            const std::string name = Text(args[1]);
            for (auto& s : e.GetComponent<ConquerorScriptComponent>().Scripts)
            {
                const std::filesystem::path p(s.ScriptPath);
                if (s.ScriptPath == name || p.filename().string() == name || p.stem().string() == name || s.ClassName == name)
                    return Value::MakeBool(true);
            }
            return Value::MakeBool(false);
        }, -1);
        // FindEntitiesWithScript("Enemy") -> [entity]: sınıf adı / dosya adı eşleşen betiği olan etkin varlıklar
        vm->DefineNative("FindEntitiesWithScript", [](int argc, Value* args) -> Value {
            auto* list = new CQSListObject();
            Scene* scene = ActiveScene();
            if (!scene || argc < 1) return Value::MakeObject(list);
            const std::string name = Text(args[0]);
            auto view = scene->m_Registry.view<ConquerorScriptComponent>();
            for (auto h : view)
            {
                if (!scene->IsEntityActive(h))
                    continue;
                for (auto& s : view.get<ConquerorScriptComponent>(h).Scripts)
                {
                    const std::filesystem::path p(s.ScriptPath);
                    if (s.ClassName == name || p.stem().string() == name || p.filename().string() == name)
                    {
                        list->Elements.push_back(Value::MakeInt((int64_t)(uint32_t)h));
                        break;
                    }
                }
            }
            return Value::MakeObject(list);
        }, 1);
        // FindEntitiesWithComponent("Camera") -> [entity]: bileşene sahip etkin varlıklar
        vm->DefineNative("FindEntitiesWithComponent", [](int argc, Value* args) -> Value {
            auto* list = new CQSListObject();
            Scene* scene = ActiveScene();
            ComponentInfo* info = argc >= 1 ? ComponentRegistry::Find(Text(args[0])) : nullptr;
            if (!scene || !info) return Value::MakeObject(list);
            auto view = scene->m_Registry.view<TagComponent>();
            for (auto h : view)
            {
                Entity e{ h, scene };
                if (scene->IsEntityActive(h) && info->Has(e))
                    list->Elements.push_back(Value::MakeInt((int64_t)(uint32_t)h));
            }
            return Value::MakeObject(list);
        }, 1);
        // AddScript(target, "Assets/Scripts/Enemy.cqs") -> bool: sonraki karenin başında Awake/Start çalışır
        vm->DefineNative("AddScript", [](int argc, Value* args) -> Value {
            Entity e = argc >= 2 ? Target(args[0]) : Entity{};
            if (!e) return Value::MakeBool(false);
            const std::string path = Text(args[1]);
            if (path.empty() || !Scripting::ScriptRuntimes::ForPath(path)) return Value::MakeBool(false);
            if (!e.HasComponent<ConquerorScriptComponent>()) e.AddComponent<ConquerorScriptComponent>();
            ConquerorScriptData data;
            data.ScriptPath = path;
            e.GetComponent<ConquerorScriptComponent>().Scripts.push_back(std::move(data));
            return Value::MakeBool(true);
        }, 2);
        // Oyun boyunca betikler (ve sahne geçişleri) arasında paylaşılan değerler: sayı / bool / metin
        vm->DefineNative("GlobalSet", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            bool representable = false;
            Scripting::ScriptValue v = Scripting::ScriptRuntimes::FromValue(args[1], &representable);
            if (!representable) return Value::MakeBool(false);
            if (v.Type == Scripting::ScriptValue::Kind::Null) Scripting::ScriptRuntimes::Globals().erase(Text(args[0]));
            else Scripting::ScriptRuntimes::Globals()[Text(args[0])] = std::move(v);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("GlobalGet", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeNull();
            auto& g = Scripting::ScriptRuntimes::Globals();
            auto it = g.find(Text(args[0]));
            if (it == g.end()) return argc > 1 ? args[1] : Value::MakeNull();
            return Scripting::ScriptRuntimes::ToValue(it->second);
        }, -1);
        vm->DefineNative("GlobalHas", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc > 0 && Scripting::ScriptRuntimes::Globals().count(Text(args[0])) != 0);
        }, 1);

        // ================================================================ Tuş simgeleri / Steam Deck
        // GetGamepadStyle() -> "Keyboard" | "Xbox" | "PlayStation" | "Nintendo" | "SteamDeck" (şu an gösterilen simgeler)
        vm->DefineNative("GetGamepadStyle", [](int, Value*) -> Value {
            return Value::MakeString(InputGlyphs::StyleName(InputGlyphs::ActiveStyle()));
        }, 0);
        // IsUsingGamepad() -> son kullanılan aygıt kumanda mı
        vm->DefineNative("IsUsingGamepad", [](int, Value*) -> Value { return Value::MakeBool(InputGlyphs::UsingGamepad()); }, 0);
        // GetButtonGlyph("a|key_space" | "Jump") -> simge dokusunun yolu (UI Image / Raw Image için)
        vm->DefineNative("GetButtonGlyph", [](int argc, Value* args) -> Value {
            return Value::MakeString(argc >= 1 ? InputGlyphs::Resolve(Text(args[0])) : std::string());
        }, 1);
        // GetButtonLabel("a") -> "A" / "Cross" / "Space" ...
        vm->DefineNative("GetButtonLabel", [](int argc, Value* args) -> Value {
            return Value::MakeString(argc >= 1 ? InputGlyphs::Label(Text(args[0])) : std::string());
        }, 1);
        // SetGlyphStyle("auto" | "keyboard" | "xbox" | "playstation" | "nintendo" | "deck")
        vm->DefineNative("SetGlyphStyle", [](int argc, Value* args) -> Value {
            InputGlyphs::SetForcedStyle(argc >= 1 ? Text(args[0]) : std::string("auto"));
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("IsSteamDeck", [](int, Value*) -> Value { return Value::MakeBool(Platform::IsSteamDeck()); }, 0);

        // ================================================================ Ses: ateşle-unut ve diğer varlıkların kaynakları
        // AudioPlayOneShot("Assets/Audio/jump.wav", volume = 1, pitch = 1, pan = 0, group = "SFX") -> tutamaç
        vm->DefineNative("AudioPlayOneShot", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeInt(0);
            const std::string path = SceneSerializer::ResolveAssetPath(Text(args[0]));
            return Value::MakeInt(AudioOneShots::Play(path, Num(argc, args, 1, 1.0f), Num(argc, args, 2, 1.0f), Num(argc, args, 3, 0.0f),
                                                      argc > 4 ? Text(args[4]) : std::string("SFX")));
        }, -1);
        // AudioPlayOneShotAt(path, x, y, z, volume = 1, pitch = 1, group = "SFX") -> tutamaç (3B konumlu)
        vm->DefineNative("AudioPlayOneShotAt", [](int argc, Value* args) -> Value {
            if (argc < 4) return Value::MakeInt(0);
            const std::string path = SceneSerializer::ResolveAssetPath(Text(args[0]));
            return Value::MakeInt(AudioOneShots::PlayAt(path, { Num(argc, args, 1), Num(argc, args, 2), Num(argc, args, 3) },
                                                        Num(argc, args, 4, 1.0f), Num(argc, args, 5, 1.0f), argc > 6 ? Text(args[6]) : std::string("SFX")));
        }, -1);
        vm->DefineNative("AudioStopOneShot", [](int argc, Value* args) -> Value {
            if (argc > 0) AudioOneShots::Stop((uint32_t)args[0].ToInt());
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("AudioIsOneShotPlaying", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc > 0 && AudioOneShots::IsPlaying((uint32_t)args[0].ToInt()));
        }, 1);
        vm->DefineNative("AudioStopAllOneShots", [](int, Value*) -> Value {
            AudioOneShots::StopAll();
            return Value::MakeNull();
        }, 0);
        // Başka bir varlığın Audio Source'u (müzik / ortam sesi nesneleri)
        vm->DefineNative("AudioPlayEntity", [](int argc, Value* args) -> Value {
            auto* asc = Get<AudioSourceComponent>(TargetArg(argc, args));
            if (!asc) return Value::MakeBool(false);
            if (auto* src = static_cast<AudioSource*>(asc->RuntimeSound))
                src->Play();
            else
                asc->PlayOnAwake = true;   // henüz yüklenmedi: yüklenince çalar
            asc->IsPlaying = true;
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("AudioStopEntity", [](int argc, Value* args) -> Value {
            auto* asc = Get<AudioSourceComponent>(TargetArg(argc, args));
            if (!asc) return Value::MakeBool(false);
            if (auto* src = static_cast<AudioSource*>(asc->RuntimeSound))
                src->Stop();
            asc->IsPlaying = false;
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("AudioSetEntityVolume", [](int argc, Value* args) -> Value {
            auto* asc = argc >= 2 ? Get<AudioSourceComponent>(Target(args[0])) : nullptr;
            if (!asc) return Value::MakeBool(false);
            asc->Volume = std::max(0.0f, Num(argc, args, 1, 1.0f));
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("AudioSetEntityPitch", [](int argc, Value* args) -> Value {
            auto* asc = argc >= 2 ? Get<AudioSourceComponent>(Target(args[0])) : nullptr;
            if (!asc) return Value::MakeBool(false);
            asc->Pitch = std::clamp(Num(argc, args, 1, 1.0f), 0.05f, 8.0f);
            return Value::MakeBool(true);
        }, 2);

        // Mikser grupları (ayar menüsü: müzik / efekt kaydırıcıları). Değerler doğrusal (1 = 0 dB); oyun bitince geri alınır
        vm->DefineNative("AudioSetGroupVolume", [](int argc, Value* args) -> Value {
            MixerGroup* g = argc >= 2 ? AudioMixer::Get().FindGroup(Text(args[0])) : nullptr;
            if (!g) return Value::MakeBool(false);
            g->VolumeDb = std::clamp(AudioMixer::LinearToDb(std::max(0.0f, Num(argc, args, 1, 1.0f))), -80.0f, 20.0f);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("AudioGetGroupVolume", [](int argc, Value* args) -> Value {
            MixerGroup* g = argc >= 1 ? AudioMixer::Get().FindGroup(Text(args[0])) : nullptr;
            return Value::MakeFloat(g ? AudioMixer::DbToLinear(g->VolumeDb) : 0.0f);
        }, 1);
        vm->DefineNative("AudioSetGroupMute", [](int argc, Value* args) -> Value {
            MixerGroup* g = argc >= 2 ? AudioMixer::Get().FindGroup(Text(args[0])) : nullptr;
            if (!g) return Value::MakeBool(false);
            g->Mute = Truthy(args[1]);
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("AudioApplySnapshot", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeBool(false);
            AudioMixer::Get().ApplySnapshot(Text(args[0]), Num(argc, args, 1, 0.5f));
            return Value::MakeBool(true);
        }, -1);

        // ================================================================ PlayerPrefs
        vm->DefineNative("PrefsSetInt", [](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeNull();
            PlayerPrefs::SetInt(Text(args[0]), args[1].IsInt() ? args[1].AsInt() : (int64_t)std::llround(args[1].ToNumber()));
            return Value::MakeNull();
        }, 2);
        vm->DefineNative("PrefsGetInt", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeInt(0);
            const int64_t def = argc > 1 && args[1].IsNumber() ? (args[1].IsInt() ? args[1].AsInt() : (int64_t)args[1].ToNumber()) : 0;
            return Value::MakeInt(PlayerPrefs::GetInt(Text(args[0]), def));
        }, -1);
        vm->DefineNative("PrefsSetFloat", [](int argc, Value* args) -> Value {
            if (argc >= 2) PlayerPrefs::SetFloat(Text(args[0]), args[1].ToNumber());
            return Value::MakeNull();
        }, 2);
        vm->DefineNative("PrefsGetFloat", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeFloat(0.0);
            return Value::MakeFloat(PlayerPrefs::GetFloat(Text(args[0]), argc > 1 && args[1].IsNumber() ? args[1].ToNumber() : 0.0));
        }, -1);
        vm->DefineNative("PrefsSetString", [](int argc, Value* args) -> Value {
            if (argc >= 2) PlayerPrefs::SetString(Text(args[0]), Text(args[1]));
            return Value::MakeNull();
        }, 2);
        vm->DefineNative("PrefsGetString", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeString("");
            return Value::MakeString(PlayerPrefs::GetString(Text(args[0]), argc > 1 ? Text(args[1]) : std::string()));
        }, -1);
        vm->DefineNative("PrefsHasKey", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc > 0 && PlayerPrefs::HasKey(Text(args[0])));
        }, 1);
        vm->DefineNative("PrefsDeleteKey", [](int argc, Value* args) -> Value {
            if (argc > 0) PlayerPrefs::DeleteKey(Text(args[0]));
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("PrefsDeleteAll", [](int, Value*) -> Value {
            PlayerPrefs::DeleteAll();
            return Value::MakeNull();
        }, 0);
        vm->DefineNative("PrefsSave", [](int, Value*) -> Value {
            return Value::MakeBool(PlayerPrefs::Save());
        }, 0);
        vm->DefineNative("GetPersistentDataPath", [](int, Value*) -> Value {
            return Value::MakeString(PlayerPrefs::GetPersistentDataPath());
        }, 0);
    }
}
