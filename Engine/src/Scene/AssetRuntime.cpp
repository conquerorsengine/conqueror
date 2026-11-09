#include "AssetRuntime.h"

#include "Core/Assets/SimpleAsset.h"
#include "Core/Data/DataTable.h"
#include "Core/Input/Input.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Renderer2D.h"
#include "Scene/CameraRig.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<FlipbookComponent>(Entity, FlipbookComponent&) {}

    namespace
    {
        struct ActiveFeedback
        {
            std::string Path;
            int Gamepad = 0;
            float Scale = 1.0f;
            float Time = 0.0f;
        };

        std::vector<ActiveFeedback>& Feedbacks()
        {
            static std::vector<ActiveFeedback> v;
            return v;
        }

        glm::vec2 s_LastFeedback{ 0.0f };
        bool s_TouchActive = false;
        std::vector<AssetRuntime::TouchControl> s_Touch;

        std::shared_ptr<Texture2D> TextureOf(const std::string& path)
        {
            static auto& cache = *new std::unordered_map<std::string, std::shared_ptr<Texture2D>>();
            auto& t = cache[path];
            if (!t && !path.empty())
                t = Texture2D::Create(SceneSerializer::ResolveAssetPath(path));
            return t;
        }

        void UpdateFlipbooks(Scene& scene, float dt)
        {
            auto view = scene.m_Registry.view<FlipbookComponent, SpriteRendererComponent>();
            for (auto e : view)
            {
                auto& f = view.get<FlipbookComponent>(e);
                if (f.Flipbook.empty())
                    continue;
                SimpleAssets::Data data;
                if (!SimpleAssets::Load(f.Flipbook, data))
                    continue;
                const auto& frames = data.Table("Frames");
                if (frames.empty())
                    continue;
                const float fps = std::max(0.01f, data.Float("Frames Per Second", 12.0f));
                float total = 0.0f;
                for (const auto& row : frames)
                    total += std::max(0.0f, SimpleAssets::CellFloat(row, 1, 1.0f)) / fps;
                if (f.Playing)
                    f.Time += dt * f.Speed;
                float t = f.Time;
                if (total > 0.0f)
                {
                    if (data.Bool("Loop", true))
                        t = std::fmod(std::max(t, 0.0f), total);
                    else
                        t = std::clamp(t, 0.0f, total - 1e-4f);
                }
                int frame = 0;
                for (size_t i = 0; i < frames.size(); ++i)
                {
                    const float length = std::max(0.0f, SimpleAssets::CellFloat(frames[i], 1, 1.0f)) / fps;
                    if (t < length)
                    {
                        frame = (int)i;
                        break;
                    }
                    t -= length;
                    frame = (int)i;
                }
                if (frame == f.Frame)
                    continue;
                f.Frame = frame;
                auto& sprite = view.get<SpriteRendererComponent>(e);
                if (auto tex = TextureOf(SimpleAssets::Cell(frames[(size_t)frame], 0)))
                {
                    sprite.Texture = tex;
                    sprite.SpriteName.clear();
                    sprite.Rect = glm::vec4(0.0f);
                }
            }
        }

        void UpdateForceFeedback(float dt)
        {
            auto& list = Feedbacks();
            std::unordered_map<int, glm::vec2> motors;
            for (auto it = list.begin(); it != list.end();)
            {
                SimpleAssets::Data data;
                if (!SimpleAssets::Load(it->Path, data))
                {
                    it = list.erase(it);
                    continue;
                }
                it->Time += dt;
                const auto& keys = data.Table("Keys");
                const float duration = keys.empty() ? 0.0f : SimpleAssets::CellFloat(keys.back(), 0);
                float t = it->Time;
                bool finished = false;
                if (duration <= 0.0f)
                    finished = true;
                else if (t >= duration)
                {
                    if (data.Bool("Looping", false))
                        t = std::fmod(t, duration);
                    else
                        finished = true;
                }
                glm::vec2 value(0.0f);
                for (size_t i = 0; i + 1 < keys.size(); ++i)
                {
                    const float a = SimpleAssets::CellFloat(keys[i], 0), b = SimpleAssets::CellFloat(keys[i + 1], 0);
                    if (t >= a && t <= b)
                    {
                        const float k = b > a ? (t - a) / (b - a) : 0.0f;
                        value.x = glm::mix(SimpleAssets::CellFloat(keys[i], 1), SimpleAssets::CellFloat(keys[i + 1], 1), k);
                        value.y = glm::mix(SimpleAssets::CellFloat(keys[i], 2), SimpleAssets::CellFloat(keys[i + 1], 2), k);
                        break;
                    }
                }
                if (!finished)
                {
                    auto& m = motors[it->Gamepad];
                    m = glm::clamp(m + value * it->Scale, 0.0f, 1.0f);
                }
                if (finished)
                {
                    Input::StopGamepadVibration(it->Gamepad);
                    it = list.erase(it);
                }
                else
                    ++it;
            }
            s_LastFeedback = glm::vec2(0.0f);
            for (const auto& [pad, m] : motors)
            {
                Input::SetGamepadVibration(m.x, m.y, pad);
                s_LastFeedback = glm::max(s_LastFeedback, m);
            }
        }
    }

    namespace AssetRuntime
    {
        void Update(Scene& scene, float dt)
        {
            UpdateFlipbooks(scene, dt);
            UpdateForceFeedback(dt);
        }

        void Reset()
        {
            for (const auto& f : Feedbacks())
                Input::StopGamepadVibration(f.Gamepad);
            Feedbacks().clear();
            DeactivateTouchInterface();
        }

        bool ActivateTouchInterface(const std::string& path)
        {
            SimpleAssets::Data data;
            if (!SimpleAssets::Load(path, data))
                return false;
            s_Touch.clear();
            for (const auto& row : data.Table("Controls"))
            {
                TouchControl c;
                c.Name = SimpleAssets::Cell(row, 0);
                c.Type = SimpleAssets::Cell(row, 1, "Joystick");
                c.Center = { SimpleAssets::CellFloat(row, 2, 0.15f), SimpleAssets::CellFloat(row, 3, 0.2f) };
                c.Radius = SimpleAssets::CellFloat(row, 4, 0.1f);
                const std::string control = SimpleAssets::Cell(row, 5, "Left Stick");
                if (control == "Left Stick") { c.AxisX = 0; c.AxisY = 1; }
                else if (control == "Right Stick") { c.AxisX = 2; c.AxisY = 3; }
                else
                {
                    static const char* kButtons[] = { "A", "B", "X", "Y", "Left Bumper", "Right Bumper", "Back", "Start" };
                    for (int i = 0; i < 8; ++i)
                        if (control == kButtons[i])
                            c.Button = i;
                }
                s_Touch.push_back(std::move(c));
            }
            s_TouchActive = !s_Touch.empty();
            if (s_TouchActive)
                Input::SetVirtualGamepad(true, "Touch Interface");
            return s_TouchActive;
        }

        void DeactivateTouchInterface()
        {
            if (s_TouchActive)
                Input::SetVirtualGamepad(false);
            s_TouchActive = false;
            s_Touch.clear();
        }

        std::vector<TouchControl>& ActiveTouchControls()
        {
            return s_Touch;
        }

        void RegisterComponents()
        {
            using C = FlipbookComponent;
            ComponentDef<C>("Flipbook Player", "FlipbookComponent", "Rendering", ICON_FA_FILM)
                .Describe("Plays a Flipbook (.cqflipbook) on this object's Sprite Renderer (Unreal Paper2D).")
                .Field("Flipbook", "Flipbook", &C::Flipbook, FieldKind::Asset).Filter(".cqflipbook")
                .Field("Playing", "Playing", &C::Playing)
                .Field("Speed", "Speed", &C::Speed)
                .Field("Frame", "Frame", &C::Frame).ReadOnly();
        }

        void RegisterNatives(Scripting::NativeRegistry* vm)
        {
            using CQS::Value;
            vm->DefineNative("ForceFeedbackPlay", [](int argc, Value* args) -> Value {
                if (argc < 1 || !args[0].IsString())
                    return Value::MakeBool(false);
                SimpleAssets::Data data;
                if (!SimpleAssets::Load(args[0].AsString()->Value, data))
                    return Value::MakeBool(false);
                Feedbacks().push_back({ args[0].AsString()->Value, argc > 1 ? (int)args[1].ToNumber() : 0, argc > 2 ? (float)args[2].ToNumber() : 1.0f, 0.0f });
                return Value::MakeBool(true);
            }, -1);
            vm->DefineNative("ForceFeedbackStop", [](int argc, Value* args) -> Value {
                auto& list = Feedbacks();
                if (argc > 0 && args[0].IsString())
                    list.erase(std::remove_if(list.begin(), list.end(), [&](const ActiveFeedback& f) { return f.Path == args[0].AsString()->Value; }), list.end());
                else
                    list.clear();
                Input::StopGamepadVibration(0);
                return Value::MakeNull();
            }, -1);
            // Son karedeki en büyük motor gücü (sol): oyun içi ölçüm / titreşim simgesi için
            vm->DefineNative("ForceFeedbackCurrent", [](int, Value*) -> Value { return Value::MakeFloat((double)s_LastFeedback.x); }, 0);
            vm->DefineNative("TouchInterfaceActivate", [](int argc, Value* args) -> Value {
                return Value::MakeBool(argc > 0 && args[0].IsString() && ActivateTouchInterface(args[0].AsString()->Value));
            }, 1);
            vm->DefineNative("TouchInterfaceDeactivate", [](int, Value*) -> Value {
                DeactivateTouchInterface();
                return Value::MakeNull();
            }, 0);
            // CameraShakePlay("Assets/X.cqcamerashake", scale = 1)
            vm->DefineNative("CameraShakePlay", [](int argc, Value* args) -> Value {
                Scene* scene = ScriptContext::GetActiveScene();
                SimpleAssets::Data data;
                if (!scene || argc < 1 || !args[0].IsString() || !SimpleAssets::Load(args[0].AsString()->Value, data))
                    return Value::MakeBool(false);
                glm::vec3 at(0.0f);
                if (Entity cam = scene->GetPrimaryCameraEntity())
                    at = glm::vec3(cam.GetComponent<TransformComponent>().WorldTransform[3]);
                const float scale = argc > 1 ? (float)args[1].ToNumber() : 1.0f;
                CameraRig::GenerateImpulseAt(*scene, at, data.Float("Amplitude", 1.0f) * scale, data.Float("Frequency", 12.0f), data.Float("Duration", 0.4f), data.Float("Radius", 1000.0f));
                return Value::MakeBool(true);
            }, -1);
            // CurveTableEvaluate("Assets/X.cqcurvetable", "Row", t): satırın değerleri başlıktaki zamanlara göre doğrusal
            vm->DefineNative("CurveTableEvaluate", [](int argc, Value* args) -> Value {
                if (argc < 3 || !args[0].IsString() || !args[1].IsString())
                    return Value::MakeFloat(0.0);
                const DataTable& table = DataTables::Load(args[0].AsString()->Value);
                const auto* row = table.FindRow(args[1].AsString()->Value);
                if (!row || table.Columns.size() < 2)
                    return Value::MakeFloat(0.0);
                const double t = args[2].ToNumber();
                double value = 0.0;
                const auto timeOf = [&](size_t i) { return std::atof(table.Columns[i].c_str()); };
                const size_t last = table.Columns.size() - 1;
                if (t <= timeOf(1))
                    value = (*row)[1].Number;
                else if (t >= timeOf(last))
                    value = (*row)[last].Number;
                else
                    for (size_t i = 1; i < last; ++i)
                        if (t >= timeOf(i) && t <= timeOf(i + 1))
                        {
                            const double k = (t - timeOf(i)) / std::max(1e-9, timeOf(i + 1) - timeOf(i));
                            value = (*row)[i].Number + ((*row)[i + 1].Number - (*row)[i].Number) * k;
                            break;
                        }
                return Value::MakeFloat(value);
            }, 3);
        }
    }
}
