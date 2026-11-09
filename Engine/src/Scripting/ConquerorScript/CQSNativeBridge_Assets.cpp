#include "CQSNativeBridge.h"
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/Visual/StateGraph.h"
#include "Scripting/TestRunner.h"

// Create menüsündeki varlık türlerinin betik çağrıları
namespace Conqueror::CQS
{
    void RegisterAssetNatives(Scripting::NativeRegistry* vm)
    {
        Scripting::StateGraphs::RegisterNatives(vm);
        Scripting::TestRunner::RegisterNatives(vm);
    }
}
