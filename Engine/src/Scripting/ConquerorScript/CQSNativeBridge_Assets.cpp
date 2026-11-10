#include "Scene/ActorComponents.h"
#include "CQSNativeBridge.h"
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/Visual/StateGraph.h"
#include "Scripting/TestRunner.h"
#include "UI/UIToolkit.h"
#include "Scene/AudioOcclusion.h"
#include "AISystem/BehaviorTree/BehaviorTreeAsset.h"
#include "Scene/AssetRuntime.h"
#include "Renderer/ExtraTextures.h"
#include "Renderer/RenderTargetRegistry.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Serialization/SceneSerializer.h"
#include "UI/UISystem.h"

#include <unordered_map>

// Create menüsündeki varlık türlerinin betik çağrıları
namespace Conqueror::CQS
{
    using Conqueror::CQS::Value;

    namespace
    {
        struct ActiveCanvas
        {
            std::shared_ptr<Texture2D> Target;
            std::vector<CanvasCommand> Commands;
        };

        ActiveCanvas s_Canvas;

        glm::vec4 ColorArg(int argc, Value* args, int index)
        {
            return glm::vec4(argc > index ? (float)args[index].ToNumber() : 1.0f, argc > index + 1 ? (float)args[index + 1].ToNumber() : 1.0f,
                             argc > index + 2 ? (float)args[index + 2].ToNumber() : 1.0f, argc > index + 3 ? (float)args[index + 3].ToNumber() : 1.0f);
        }
    }

    void RegisterAssetNatives(Scripting::NativeRegistry* vm)
    {
        // Material Parameter Collection: MPCSetScalar("Assets/X.cqmpc", "Name", value), MPCSetVector(path, name, r, g, b, a)
        vm->DefineNative("MPCSetScalar", [](int argc, Value* args) -> Value {
            if (argc < 3 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
            MaterialParameterCollections::SetScalar(args[0].AsString()->Value, args[1].AsString()->Value, (float)args[2].ToNumber());
            return Value::MakeBool(true);
        }, 3);
        vm->DefineNative("MPCSetVector", [](int argc, Value* args) -> Value {
            if (argc < 3 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
            MaterialParameterCollections::SetVector(args[0].AsString()->Value, args[1].AsString()->Value, ColorArg(argc, args, 2));
            return Value::MakeBool(true);
        }, -1);
        // Canvas Render Target: CanvasBegin(path) ... CanvasEnd(); ardından doku her yerde kullanılabilir
        vm->DefineNative("CanvasBegin", [](int argc, Value* args) -> Value {
            if (argc < 1 || !args[0].IsString()) return Value::MakeBool(false);
            s_Canvas.Target = Texture2D::Create(SceneSerializer::ResolveAssetPath(args[0].AsString()->Value));
            s_Canvas.Commands.clear();
            return Value::MakeBool(s_Canvas.Target != nullptr);
        }, 1);
        vm->DefineNative("CanvasClear", [](int argc, Value* args) -> Value {
            CanvasCommand c;
            c.Type = CanvasCommand::Kind::Clear;
            c.Color = ColorArg(argc, args, 0);
            s_Canvas.Commands.push_back(std::move(c));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("CanvasDrawRect", [](int argc, Value* args) -> Value {
            if (argc < 4) return Value::MakeNull();
            CanvasCommand c;
            c.Type = CanvasCommand::Kind::Rect;
            c.Box = glm::vec4((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber(), (float)args[3].ToNumber());
            c.Color = ColorArg(argc, args, 4);
            s_Canvas.Commands.push_back(std::move(c));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("CanvasDrawText", [](int argc, Value* args) -> Value {
            if (argc < 3 || !args[2].IsString()) return Value::MakeNull();
            CanvasCommand c;
            c.Type = CanvasCommand::Kind::Text;
            c.Box = glm::vec4((float)args[0].ToNumber(), (float)args[1].ToNumber(), 0.0f, 0.0f);
            c.Text = args[2].AsString()->Value;
            c.Size = argc > 3 ? (float)args[3].ToNumber() : 24.0f;
            c.Color = ColorArg(argc, args, 4);
            s_Canvas.Commands.push_back(std::move(c));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("CanvasDrawTexture", [](int argc, Value* args) -> Value {
            if (argc < 5 || !args[0].IsString()) return Value::MakeNull();
            CanvasCommand c;
            c.Type = CanvasCommand::Kind::Image;
            c.Text = args[0].AsString()->Value;
            c.Box = glm::vec4((float)args[1].ToNumber(), (float)args[2].ToNumber(), (float)args[3].ToNumber(), (float)args[4].ToNumber());
            c.Color = ColorArg(argc, args, 5);
            s_Canvas.Commands.push_back(std::move(c));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("CanvasEnd", [](int, Value*) -> Value {
            auto* target = dynamic_cast<RenderTargetTexture*>(s_Canvas.Target.get());
            if (target)
                UISystem::DrawCanvas(target->GetFramebuffer(), target->GetWidth(), target->GetHeight(), s_Canvas.Commands);
            s_Canvas.Commands.clear();
            s_Canvas.Target.reset();
            return Value::MakeBool(target != nullptr);
        }, 0);

        Scripting::StateGraphs::RegisterNatives(vm);
        Scripting::TestRunner::RegisterNatives(vm);
        UIToolkit::RegisterNatives(vm);
        BehaviorTrees::RegisterNatives(vm);
        AssetRuntime::RegisterNatives(vm);
        ActorComponents::RegisterNatives(vm);
        // Ses varlıkları
        vm->DefineNative("SoundMixPush", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc > 0 && args[0].IsString() && SoundMixes::Push(args[0].AsString()->Value));
        }, 1);
        vm->DefineNative("SoundMixPop", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc > 0 && args[0].IsString() && SoundMixes::Pop(args[0].AsString()->Value));
        }, 1);
        vm->DefineNative("DialoguePlay", [](int argc, Value* args) -> Value {
            return Value::MakeBool(argc > 0 && args[0].IsString() && Dialogue::Play(args[0].AsString()->Value, argc > 1 ? (float)args[1].ToNumber() : 1.0f));
        }, -1);
    }
}
