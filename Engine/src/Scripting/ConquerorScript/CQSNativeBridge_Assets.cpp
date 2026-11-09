#include "CQSNativeBridge.h"
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/Visual/StateGraph.h"
#include "Scripting/TestRunner.h"
#include "UI/UIToolkit.h"
#include "Scene/AudioOcclusion.h"
#include "AISystem/BehaviorTree/BehaviorTreeAsset.h"

// Create menüsündeki varlık türlerinin betik çağrıları
namespace Conqueror::CQS
{
    using Conqueror::CQS::Value;

    void RegisterAssetNatives(Scripting::NativeRegistry* vm)
    {
        Scripting::StateGraphs::RegisterNatives(vm);
        Scripting::TestRunner::RegisterNatives(vm);
        UIToolkit::RegisterNatives(vm);
        BehaviorTrees::RegisterNatives(vm);
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
