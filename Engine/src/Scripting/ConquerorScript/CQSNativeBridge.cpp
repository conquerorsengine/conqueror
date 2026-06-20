#include "CQSNativeBridge.h"
#include "CQSVM.h"
#include "CQSEngine.h"
#include "Core/Logging/Log.h"
#include "Core/Input/Input.h"
#include "Core/Time/TimeManager.h"
#include "Core/PhysicsSystem/Physics.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioSource.h"
#include "Core/Math/Math.h"
#include "Networking/NetworkManager.h"
#include "Networking/WebClient.h"
#include "Core/Base/String.h"
#include "Core/Base/ListUtils.h"
#include "Core/Base/MapUtils.h"
#include "Core/Utils/Crypto.h"
#include "Core/Time/DateUtils.h"
#include "Core/Utils/JSON.h"
#include "Core/Utils/Compression.h"
#include "Core/Utils/ImageUtils.h"
#include "Core/Utils/Platform.h"
#include "Core/Utils/UUID.h"
#include "Core/Utils/URLUtils.h"
#include "Core/Utils/FileSystem.h"
#include "Core/Utils/RegexUtils.h"
#include "Core/Utils/SaveSystem.h"
#include "Core/Math/GeomUtils.h"
#include "Core/Math/ExpressionParser.h"
#include "Core/Math/MathExtended.h"
#include "Core/Math/MLUtils.h"
#include "Core/Math/Curve.h"
#include "Core/Base/Hash.h"
#include "Core/Math/Random.h"
#include "Core/Math/VecMath.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/QuaternionMath.h"
#include "Core/Math/TransformMath.h"
#include "Core/Math/Easing.h"
#include "Core/Math/Noise.h"
#include "Core/PhysicsSystem/PhysicsMath.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <regex>
#include <thread>
#include <algorithm>

namespace Conqueror::CQS
{
    // ── Helper ──
    static Entity GetCurrentEntity()
    {
        auto* vm = CQSEngine::GetVM();
        auto* scene = static_cast<Scene*>(CQSEngine::GetActiveScene());
        if (!vm || !scene) return Entity{};
        auto* inst = vm->GetCurrentInstance();
        if (!inst || inst->EntityHandle == 0xFFFFFFFF) return Entity{};
        entt::entity handle = static_cast<entt::entity>(inst->EntityHandle);
        if (!scene->m_Registry.valid(handle)) return Entity{};
        return Entity{ handle, scene };
    }

    static CQSMapObject* MakeVec3Map(float x, float y, float z)
    {
        auto* m = new CQSMapObject();
        m->Entries["x"] = Value::MakeFloat(x);
        m->Entries["y"] = Value::MakeFloat(y);
        m->Entries["z"] = Value::MakeFloat(z);
        return m;
    }

    static CQSMapObject* MakeVec2Map(float x, float y)
    {
        auto* m = new CQSMapObject();
        m->Entries["x"] = Value::MakeFloat(x);
        m->Entries["y"] = Value::MakeFloat(y);
        return m;
    }

    // ════════════════════════════════════════
    //  RegisterGlobals
    // ════════════════════════════════════════
    void CQSNativeBridge::RegisterGlobals(CQSVM* vm)
    {
        // Log
        vm->DefineNative("print", LogInfo, 1);
        vm->DefineNative("LogInfo", LogInfo, 1);
        vm->DefineNative("LogWarn", LogWarn, 1);
        vm->DefineNative("LogError", LogError, 1);

        // Transform
        vm->DefineNative("GetPosition", GetPosition, 0);
        vm->DefineNative("SetPosition", SetPosition, 3);
        vm->DefineNative("GetRotation", GetRotation, 0);
        vm->DefineNative("SetRotation", SetRotation, 3);
        vm->DefineNative("GetScale", GetScale, 0);
        vm->DefineNative("SetScale", SetScale, 3);
        vm->DefineNative("GetForward", GetForward, 0);
        vm->DefineNative("GetRight", GetRight, 0);
        vm->DefineNative("GetUp", GetUp, 0);

        // Physics 3D
        vm->DefineNative("GetLinearVelocity", GetLinearVelocity, 0);
        vm->DefineNative("SetLinearVelocity", SetLinearVelocity, 3);
        vm->DefineNative("GetAngularVelocity", GetAngularVelocity, 0);
        vm->DefineNative("SetAngularVelocity", SetAngularVelocity, 3);
        vm->DefineNative("ApplyForce", ApplyForce, 3);
        vm->DefineNative("ApplyImpulse", ApplyImpulse, 3);
        vm->DefineNative("ApplyTorque", ApplyTorque, 3);

        // Physics 2D
        vm->DefineNative("GetLinearVelocity2D", GetLinearVelocity2D, 0);
        vm->DefineNative("SetLinearVelocity2D", SetLinearVelocity2D, 2);
        vm->DefineNative("GetAngularVelocity2D", GetAngularVelocity2D, 0);
        vm->DefineNative("SetAngularVelocity2D", SetAngularVelocity2D, 1);
        vm->DefineNative("ApplyForce2D", ApplyForce2D, 2);
        vm->DefineNative("ApplyImpulse2D", ApplyImpulse2D, 2);

        // Input: Keyboard
        vm->DefineNative("IsKeyPressed", IsKeyPressed, 1);
        vm->DefineNative("IsKeyDown", IsKeyDown, 1);
        vm->DefineNative("IsKeyUp", IsKeyUp, 1);

        // Input: Mouse
        vm->DefineNative("IsMouseButtonPressed", IsMouseButtonPressed, 1);
        vm->DefineNative("IsMouseButtonDown", IsMouseButtonDown, 1);
        vm->DefineNative("IsMouseButtonUp", IsMouseButtonUp, 1);
        vm->DefineNative("GetMousePosition", GetMousePosition, 0);
        vm->DefineNative("GetMouseX", GetMouseX, 0);
        vm->DefineNative("GetMouseY", GetMouseY, 0);
        vm->DefineNative("GetMouseDelta", GetMouseDelta, 0);
        vm->DefineNative("GetMouseScrollDelta", GetMouseScrollDelta, 0);
        vm->DefineNative("SetCursorMode", SetCursorMode, 1);

        // Input: Gamepad
        vm->DefineNative("IsGamepadConnected", IsGamepadConnected, 0);
        vm->DefineNative("IsGamepadButtonPressed", IsGamepadButtonPressed, 2);
        vm->DefineNative("GetGamepadAxis", GetGamepadAxis, 2);
        vm->DefineNative("GetGamepadLeftStick", GetGamepadLeftStick, 1);
        vm->DefineNative("GetGamepadRightStick", GetGamepadRightStick, 1);
        vm->DefineNative("GetGamepadLeftTrigger", GetGamepadLeftTrigger, 1);
        vm->DefineNative("GetGamepadRightTrigger", GetGamepadRightTrigger, 1);
        vm->DefineNative("SetGamepadVibration", SetGamepadVibration, 3);

        vm->DefineNative("InputCreateContext", InputCreateContext, 2);
        vm->DefineNative("InputSwitchContext", InputSwitchContext, 1);
        vm->DefineNative("InputPushContext", InputPushContext, 1);
        vm->DefineNative("InputPopContext", InputPopContext, 0);
        vm->DefineNative("InputBindAction", InputBindAction, 3);
        vm->DefineNative("InputBindChord", InputBindChord, 4);
        vm->DefineNative("InputGetAction", InputGetAction, 1);
        vm->DefineNative("InputGetActionState", InputGetActionState, 1);
        vm->DefineNative("InputGetActionAxis1D", InputGetActionAxis1D, 1);
        vm->DefineNative("InputGetActionAxis2D", InputGetActionAxis2D, 1);
        vm->DefineNative("InputIsComboTriggered", InputIsComboTriggered, 1);
        vm->DefineNative("InputGetAnyKey", InputGetAnyKey, 0);

        // Audio
        vm->DefineNative("AudioPlay", AudioPlay, 0);
        vm->DefineNative("AudioStop", AudioStop, 0);
        vm->DefineNative("AudioPause", AudioPause, 0);
        vm->DefineNative("AudioSetVolume", AudioSetVolume, 1);
        vm->DefineNative("AudioGetVolume", AudioGetVolume, 0);
        vm->DefineNative("AudioSetPitch", AudioSetPitch, 1);
        vm->DefineNative("AudioSetLoop", AudioSetLoop, 1);
        vm->DefineNative("AudioIsPlaying", AudioIsPlaying, 0);
        vm->DefineNative("AudioSetMasterVolume", AudioSetMasterVolume, 1);
        vm->DefineNative("AudioGetMasterVolume", AudioGetMasterVolume, 0);

        // Time
        vm->DefineNative("GetDeltaTime", GetDeltaTime, 0);
        vm->DefineNative("GetUnscaledDeltaTime", GetUnscaledDeltaTime, 0);
        vm->DefineNative("GetTime", GetTime, 0);
        vm->DefineNative("GetFrameCount", GetFrameCount, 0);
        vm->DefineNative("GetTimeScale", GetTimeScale, 0);
        vm->DefineNative("SetTimeScale", SetTimeScale, 1);

        // Scene / Entity
        vm->DefineNative("FindEntityByTag", FindEntityByTag, 1);
        vm->DefineNative("GetEntityTag", GetEntityTag, 0);
        vm->DefineNative("SetEntityTag", SetEntityTag, 1);
        vm->DefineNative("DestroyEntity", DestroyEntity, 0);
        vm->DefineNative("CreateEntity", CreateEntity, 1);
        vm->DefineNative("GetEntityName", GetEntityName, 0);

        // Component Queries
        vm->DefineNative("HasRigidbody", HasRigidbody, 0);
        vm->DefineNative("HasAudioSource", HasAudioSource, 0);
        vm->DefineNative("HasAnimator", HasAnimator, 0);
        vm->DefineNative("HasCamera", HasCamera, 0);

        // Animator
        vm->DefineNative("AnimatorPlay", AnimatorPlay, 0);
        vm->DefineNative("AnimatorStop", AnimatorStop, 0);
        vm->DefineNative("AnimatorSetSpeed", AnimatorSetSpeed, 1);
        vm->DefineNative("AnimatorGetSpeed", AnimatorGetSpeed, 0);
        vm->DefineNative("AnimatorSetClip", AnimatorSetClip, 1);

        // Renderer
        vm->DefineNative("SetColor", SetColor, 4);
        vm->DefineNative("GetColor", GetColor, 0);
        vm->DefineNative("SetVisible", SetVisible, 1);

        // Camera
        vm->DefineNative("GetCameraFOV", GetCameraFOV, 0);
        vm->DefineNative("SetCameraFOV", SetCameraFOV, 1);
        
        // Navigation
        vm->DefineNative("NavSetDestination", NavSetDestination, 3);
        vm->DefineNative("NavBake", NavBake, 0);
        vm->DefineNative("NavStop", NavStop, 0);
        vm->DefineNative("NavIsStopped", NavIsStopped, 0);
        vm->DefineNative("NavHasPath", NavHasPath, 0);
        vm->DefineNative("NavGetSpeed", NavGetSpeed, 0);
        vm->DefineNative("NavSetSpeed", NavSetSpeed, 1);

        // Networking
        vm->DefineNative("NetStartServer", NetStartServer, 1);
        vm->DefineNative("NetConnect", NetConnect, 2);
        vm->DefineNative("NetDisconnect", NetDisconnect, 0);
        vm->DefineNative("NetSend", NetSend, 2);
        vm->DefineNative("NetBroadcast", NetBroadcast, 1);
        vm->DefineNative("NetIsServer", NetIsServer, 0);
        vm->DefineNative("NetIsClient", NetIsClient, 0);
        vm->DefineNative("NetIsConnected", NetIsConnected, 0);
        vm->DefineNative("NetGetPeerCount", NetGetPeerCount, 0);

        // Networking (Advanced)
        vm->DefineNative("NetEnableEncryption", NetEnableEncryption, 0);
        vm->DefineNative("NetIsEncryptionEnabled", NetIsEncryptionEnabled, 0);
        vm->DefineNative("NetOpenNATPort", NetOpenNATPort, 1);
        vm->DefineNative("NetCloseNATPort", NetCloseNATPort, 1);
        vm->DefineNative("NetGetExternalIP", NetGetExternalIP, 0);
        vm->DefineNative("NetCallRPC", NetCallRPC, 1);

        // Math
        vm->DefineNative("MathSin", MathSin, 1);
        vm->DefineNative("MathCos", MathCos, 1);
        vm->DefineNative("MathTan", MathTan, 1);
        vm->DefineNative("MathSqrt", MathSqrt, 1);
        vm->DefineNative("MathAbs", MathAbs, 1);
        vm->DefineNative("MathFloor", MathFloor, 1);
        vm->DefineNative("MathCeil", MathCeil, 1);
        vm->DefineNative("MathClamp", MathClamp, 3);
        vm->DefineNative("MathLerp", MathLerp, 3);
        vm->DefineNative("MathMin", MathMin, 2);
        vm->DefineNative("MathMax", MathMax, 2);
        vm->DefineNative("MathPow", MathPow, 2);
        vm->DefineNative("MathRandom", MathRandom, 0);

        // Advanced Math
        vm->DefineNative("MathRGBToHSL", MathRGBToHSL, 3);
        vm->DefineNative("MathHSLToRGB", MathHSLToRGB, 3);
        vm->DefineNative("MathEvaluateEasing", MathEvaluateEasing, 2);
        vm->DefineNative("MathRayCapsuleIntersect", MathRayCapsuleIntersect, 2);
        vm->DefineNative("MathComputeMinimalBoundingSphere", MathComputeMinimalBoundingSphere, 1);
        vm->DefineNative("MathRunThreadSafetyTest", MathRunThreadSafetyTest, 0);

        // Math Phase 14
        vm->DefineNative("MathSHEvaluateLight", MathSHEvaluateLight, 2);
        vm->DefineNative("MathSpatialCrossMotion", MathSpatialCrossMotion, 4);
        vm->DefineNative("MathDelaunayTriangulate", MathDelaunayTriangulate, 1);
        vm->DefineNative("MathHashMurmur3", MathHashMurmur3, 2);
        vm->DefineNative("MathCurveNURBS", MathCurveNURBS, 5);
        vm->DefineNative("MathSignalDCT", MathSignalDCT, 1);

        // Math Phase 15
        vm->DefineNative("MathTensorOuterProduct", MathTensorOuterProduct, 2);
        vm->DefineNative("MathGraphDijkstra", MathGraphDijkstra, 3);
        vm->DefineNative("MathFluidSPHPoly6", MathFluidSPHPoly6, 2);
        vm->DefineNative("MathDiffGeomCurvature", MathDiffGeomCurvature, 2);
        vm->DefineNative("MathCellularConway", MathCellularConway, 3);

        // Math Phase 16
        vm->DefineNative("MathFourierFFT", MathFourierFFT, 2);
        vm->DefineNative("MathSphericalHaversine", MathSphericalHaversine, 5);
        vm->DefineNative("MathGeometricAlgebraMultivectorMul", MathGeometricAlgebraMultivectorMul, 2);
        vm->DefineNative("MathMarkovNextState", MathMarkovNextState, 3);
        vm->DefineNative("MathFuzzyTrapezoid", MathFuzzyTrapezoid, 5);
        vm->DefineNative("MathVoronoiLloydRelax", MathVoronoiLloydRelax, 4);

        // Logging
        vm->DefineNative("LogRegisterCategory", LogRegisterCategory, 2);
        vm->DefineNative("LogSetLevel", LogSetLevel, 2);
        vm->DefineNative("LogGenerateCrashDump", LogGenerateCrashDump, 1);
        vm->DefineNative("LogGetHistoryCount", LogGetHistoryCount, 0);
        vm->DefineNative("LogJSON", LogJSON, 2);
        vm->DefineNative("LogPushContext", LogPushContext, 1);
        vm->DefineNative("LogPopContext", LogPopContext, 0);
        vm->DefineNative("LogSendWebhookAlert", LogSendWebhookAlert, 1);
        vm->DefineNative("LogEnableNetworkBroadcaster", LogEnableNetworkBroadcaster, 1);
        vm->DefineNative("LogMetricIncrement", LogMetricIncrement, 2);
        vm->DefineNative("LogMetricSetGauge", LogMetricSetGauge, 2);
        vm->DefineNative("LogMetricRecordTime", LogMetricRecordTime, 2);
        vm->DefineNative("LogDumpMetricsToLog", LogDumpMetricsToLog, 0);
        vm->DefineNative("LogSetCategoryEnabled", LogSetCategoryEnabled, 2);
        vm->DefineNative("LogIsCategoryEnabled", LogIsCategoryEnabled, 1);
        vm->DefineNative("LogSetGlobalLevel", LogSetGlobalLevel, 1);
        vm->DefineNative("LogGetGlobalLevel", LogGetGlobalLevel, 0);
        vm->DefineNative("LogExportHistoryToHtml", LogExportHistoryToHtml, 1);
        vm->DefineNative("LogExportHistoryToJson", LogExportHistoryToJson, 1);
        vm->DefineNative("LogExportProfilerSession", LogExportProfilerSession, 1);
        vm->DefineNative("LogClearLogHistory", LogClearLogHistory, 0);
        vm->DefineNative("LogFlushAll", LogFlushAll, 0);
        vm->DefineNative("LogDisableNetworkBroadcaster", LogDisableNetworkBroadcaster, 0);
        vm->DefineNative("LogGetCurrentContext", LogGetCurrentContext, 0);

        // DebugDraw
        vm->DefineNative("DebugDrawLine", DebugDrawLine, 3);
        vm->DefineNative("DebugDrawBox", DebugDrawBox, 3);
        vm->DefineNative("DebugDrawSolidBox", DebugDrawSolidBox, 3);
        vm->DefineNative("DebugDrawSphere", DebugDrawSphere, 3);
        vm->DefineNative("DebugDrawSolidSphere", DebugDrawSolidSphere, 3);
        vm->DefineNative("DebugDrawText3D", DebugDrawText3D, 3);
        vm->DefineNative("DebugDrawRay", DebugDrawRay, 3);
        vm->DefineNative("DebugDrawCapsule", DebugDrawCapsule, 3);
        vm->DefineNative("DebugDrawGrid", DebugDrawGrid, 3);
        vm->DefineNative("DebugDrawCoordinateAxes", DebugDrawCoordinateAxes, 2);
        vm->DefineNative("DebugDrawVelocity", DebugDrawVelocity, 3);
        vm->DefineNative("DebugDrawClear", DebugDrawClear, 0);
        vm->DefineNative("DebugDrawClearTimed", DebugDrawClearTimed, 0);
        vm->DefineNative("DebugDrawSetEnabled", DebugDrawSetEnabled, 1);

        // Vec helpers
        vm->DefineNative("Vec3Length", Vec3Length, 1);
        vm->DefineNative("Vec3Normalize", Vec3Normalize, 1);
        vm->DefineNative("Vec3Dot", Vec3Dot, 2);
        vm->DefineNative("Vec3Cross", Vec3Cross, 2);
        vm->DefineNative("Vec3Distance", Vec3Distance, 2);
        vm->DefineNative("MakeVec3", MakeVec3, 3);
        vm->DefineNative("MakeVec2", MakeVec2, 2);

        // ── Standard Library: String ──
        vm->DefineNative("StringLength", StringLength, 1);
        vm->DefineNative("StringSubstring", StringSubstring, 3);
        vm->DefineNative("StringToUpper", StringToUpper, 1);
        vm->DefineNative("StringToLower", StringToLower, 1);
        vm->DefineNative("StringIndexOf", StringIndexOf, 2);
        vm->DefineNative("StringReplace", StringReplace, 3);
        vm->DefineNative("StringSplit", StringSplit, 2);
        vm->DefineNative("StringTrim", StringTrim, 1);
        vm->DefineNative("StringStartsWith", StringStartsWith, 2);
        vm->DefineNative("StringEndsWith", StringEndsWith, 2);
        vm->DefineNative("StringContains", StringContains, 2);
        vm->DefineNative("StringPadLeft", StringPadLeft, 3);
        vm->DefineNative("StringPadRight", StringPadRight, 3);
        vm->DefineNative("StringIsAlpha", StringIsAlpha, 1);
        vm->DefineNative("StringIsDigit", StringIsDigit, 1);
        vm->DefineNative("StringIsAlnum", StringIsAlnum, 1);
        vm->DefineNative("StringReverse", StringReverse, 1);

        // ── Standard Library: List ──
        vm->DefineNative("ListAdd", ListAdd, 2);
        vm->DefineNative("ListRemoveAt", ListRemoveAt, 2);
        vm->DefineNative("ListInsert", ListInsert, 3);
        vm->DefineNative("ListClear", ListClear, 1);
        vm->DefineNative("ListSize", ListSize, 1);
        vm->DefineNative("ListContains", ListContains, 2);
        vm->DefineNative("ListIndexOf", ListIndexOf, 2);
        vm->DefineNative("ListJoin", ListJoin, 2);
        vm->DefineNative("ListReverse", ListReverse, 1);
        vm->DefineNative("ListShuffle", ListShuffle, 1);
        vm->DefineNative("ListSwap", ListSwap, 3);
        vm->DefineNative("ListCount", ListCount, 2);
        vm->DefineNative("ListMin", ListMin, 1);
        vm->DefineNative("ListMax", ListMax, 1);
        vm->DefineNative("ListSum", ListSum, 1);
        vm->DefineNative("ListAverage", ListAverage, 1);

        // ── Standard Library: Map ──
        vm->DefineNative("MapSet", MapSet, 3);
        vm->DefineNative("MapGet", MapGet, 2);
        vm->DefineNative("MapHasKey", MapHasKey, 2);
        vm->DefineNative("MapRemove", MapRemove, 2);
        vm->DefineNative("MapKeys", MapKeys, 1);
        vm->DefineNative("MapValues", MapValues, 1);
        vm->DefineNative("MapSize", MapSize, 1);
        vm->DefineNative("MapClear", MapClear, 1);

        // ── Standard Library: Extended Math ──
        vm->DefineNative("MathRandomInt", MathRandomInt, 2);
        vm->DefineNative("MathRandomRange", MathRandomRange, 2);
        vm->DefineNative("MathSign", MathSign, 1);
        vm->DefineNative("MathRound", MathRound, 1);
        vm->DefineNative("MathTrunc", MathTrunc, 1);
        vm->DefineNative("MathLog", MathLog, 1);
        vm->DefineNative("MathLog10", MathLog10, 1);
        vm->DefineNative("MathExp", MathExp, 1);
        vm->DefineNative("MathAsin", MathAsin, 1);
        vm->DefineNative("MathAcos", MathAcos, 1);
        vm->DefineNative("MathAtan", MathAtan, 1);
        vm->DefineNative("MathAtan2", MathAtan2, 2);
        vm->DefineNative("MathSinh", MathSinh, 1);
        vm->DefineNative("MathCosh", MathCosh, 1);
        vm->DefineNative("MathTanh", MathTanh, 1);

        // ── Standard Library: Crypto & Encode ──
        vm->DefineNative("Base64Encode", Base64Encode, 1);
        vm->DefineNative("Base64Decode", Base64Decode, 1);
        vm->DefineNative("HashMD5Fallback", HashMD5Fallback, 1);

        // ── Standard Library Phase 2: String Extras ──
        vm->DefineNative("StringCount", StringCount, 2);
        vm->DefineNative("StringRepeat", StringRepeat, 2);
        vm->DefineNative("StringCapitalize", StringCapitalize, 1);
        vm->DefineNative("StringCharAt", StringCharAt, 2);
        vm->DefineNative("StringCharCodeAt", StringCharCodeAt, 2);
        vm->DefineNative("StringFromCharCode", StringFromCharCode, 1);
        vm->DefineNative("StringTrimLeft", StringTrimLeft, 1);
        vm->DefineNative("StringTrimRight", StringTrimRight, 1);
        vm->DefineNative("StringLastIndexOf", StringLastIndexOf, 2);

        // ── Standard Library Phase 2: List Extras ──
        vm->DefineNative("ListSort", ListSort, 1);
        vm->DefineNative("ListSlice", ListSlice, 3);
        vm->DefineNative("ListFlatten", ListFlatten, 1);
        vm->DefineNative("ListUnique", ListUnique, 1);
        vm->DefineNative("ListFill", ListFill, 2);
        vm->DefineNative("ListRepeat", ListRepeat, 2);
        vm->DefineNative("ListPop", ListPop, 1);

        // ── Standard Library Phase 2: Map Extras ──
        vm->DefineNative("MapMerge", MapMerge, 2);
        vm->DefineNative("MapEntries", MapEntries, 1);

        // ── Standard Library Phase 2: Extended Math ──
        vm->DefineNative("MathStandardDeviation", MathStandardDeviation, 1);
        vm->DefineNative("MathVariance", MathVariance, 1);
        vm->DefineNative("MathMedian", MathMedian, 1);
        vm->DefineNative("MathFactorial", MathFactorial, 1);
        vm->DefineNative("MathIsPrime", MathIsPrime, 1);
        vm->DefineNative("MathGCD", MathGCD, 2);
        vm->DefineNative("MathLCM", MathLCM, 2);
        vm->DefineNative("MathDegToRad", MathDegToRad, 1);
        vm->DefineNative("MathRadToDeg", MathRadToDeg, 1);
        vm->DefineNative("MathFmod", MathFmod, 2);
        vm->DefineNative("MathHypot", MathHypot, 2);
        vm->DefineNative("MathCbrt", MathCbrt, 1);

        // ── Standard Library Phase 2: Date / Time ──
        vm->DefineNative("DateNow", DateNow, 0);
        vm->DefineNative("DateGetYear", DateGetYear, 1);
        vm->DefineNative("DateGetMonth", DateGetMonth, 1);
        vm->DefineNative("DateGetDay", DateGetDay, 1);
        vm->DefineNative("DateGetHours", DateGetHours, 1);
        vm->DefineNative("DateGetMinutes", DateGetMinutes, 1);
        vm->DefineNative("DateGetSeconds", DateGetSeconds, 1);
        vm->DefineNative("DateFormat", DateFormat, 2);
        vm->DefineNative("DateGetDayOfWeek", DateGetDayOfWeek, 1);
        vm->DefineNative("DateGetDayOfYear", DateGetDayOfYear, 1);
        vm->DefineNative("DateIsLeapYear", DateIsLeapYear, 1);
        vm->DefineNative("DateDiffSeconds", DateDiffSeconds, 2);

        // ── Standard Library Phase 2: Path Utilities ──
        vm->DefineNative("PathGetExtension", PathGetExtension, 1);
        vm->DefineNative("PathGetFileName", PathGetFileName, 1);
        vm->DefineNative("PathGetFileNameWithoutExtension", PathGetFileNameWithoutExtension, 1);
        vm->DefineNative("PathGetDirectoryName", PathGetDirectoryName, 1);
        vm->DefineNative("PathCombine", PathCombine, 2);
        vm->DefineNative("PathIsAbsolute", PathIsAbsolute, 1);

        // ── Standard Library Phase 2: Color Utilities ──
        vm->DefineNative("ColorHexToRGB", ColorHexToRGB, 1);
        vm->DefineNative("ColorRGBToHex", ColorRGBToHex, 3);
        vm->DefineNative("ColorLerp", ColorLerp, 3);
        vm->DefineNative("ColorInvert", ColorInvert, 3);
        vm->DefineNative("ColorBrightness", ColorBrightness, 3);

        // ── Standard Library Phase 2: Geometry ──
        vm->DefineNative("GeomDistance2D", GeomDistance2D, 4);
        vm->DefineNative("GeomCircleIntersect", GeomCircleIntersect, 6);
        vm->DefineNative("GeomAABBIntersect", GeomAABBIntersect, 8);
        vm->DefineNative("GeomPolygonArea", GeomPolygonArea, 1);
        vm->DefineNative("GeomPointInRect", GeomPointInRect, 6);
        vm->DefineNative("GeomAngleBetween", GeomAngleBetween, 4);

        // ── Standard Library Phase 2: Regex ──
        vm->DefineNative("RegexIsMatch", RegexIsMatch, 2);
        vm->DefineNative("RegexReplace", RegexReplace, 3);

        // ── Standard Library Phase 2: Environment ──
        vm->DefineNative("EnvGetOS", EnvGetOS, 0);
        vm->DefineNative("EnvGetProcessorCount", EnvGetProcessorCount, 0);

        // ── Standard Library Phase 2: Type Utilities ──
        vm->DefineNative("TypeOf", TypeOf, 1);
        vm->DefineNative("ToStringNative", ToStringNative, 1);
        vm->DefineNative("ToNumberNative", ToNumberNative, 1);
        vm->DefineNative("ParseInt", ParseInt, 1);
        vm->DefineNative("ParseFloat", ParseFloat, 1);

        // ── Standard Library Phase 3: Vec3 Extras ──
        vm->DefineNative("Vec3Add", Vec3Add, 2);
        vm->DefineNative("Vec3Sub", Vec3Sub, 2);
        vm->DefineNative("Vec3Mul", Vec3Mul, 2);
        vm->DefineNative("Vec3Div", Vec3Div, 2);
        vm->DefineNative("Vec3Lerp", Vec3Lerp, 3);
        vm->DefineNative("Vec3Reflect", Vec3Reflect, 2);

        // ── Standard Library Phase 3: Matrix 4x4 ──
        vm->DefineNative("Mat4Identity", Mat4Identity, 0);
        vm->DefineNative("Mat4Translate", Mat4Translate, 2);
        vm->DefineNative("Mat4Rotate", Mat4Rotate, 3);
        vm->DefineNative("Mat4Scale", Mat4Scale, 2);
        vm->DefineNative("Mat4Multiply", Mat4Multiply, 2);
        vm->DefineNative("Mat4Inverse", Mat4Inverse, 1);
        vm->DefineNative("Mat4Transpose", Mat4Transpose, 1);
        vm->DefineNative("Mat4LookAt", Mat4LookAt, 3);
        vm->DefineNative("Mat4Perspective", Mat4Perspective, 4);
        vm->DefineNative("Mat4Ortho", Mat4Ortho, 6);

        // ── Standard Library Phase 3: Bitwise ──
        vm->DefineNative("BitAnd", BitAnd, 2);
        vm->DefineNative("BitOr", BitOr, 2);
        vm->DefineNative("BitXor", BitXor, 2);
        vm->DefineNative("BitNot", BitNot, 1);
        vm->DefineNative("BitShiftLeft", BitShiftLeft, 2);
        vm->DefineNative("BitShiftRight", BitShiftRight, 2);

        // ── Standard Library Phase 3: Random Advanced ──
        vm->DefineNative("RandGaussian", RandGaussian, 2);
        vm->DefineNative("RandUUID", RandUUID, 0);
        vm->DefineNative("RandColor", RandColor, 0);
        vm->DefineNative("RandInUnitSphere", RandInUnitSphere, 0);
        vm->DefineNative("RandInUnitCircle", RandInUnitCircle, 0);

        // ── Standard Library Phase 3: File IO ──
        vm->DefineNative("FileExists", FileExists, 1);
        vm->DefineNative("FileReadText", FileReadText, 1);
        vm->DefineNative("FileWriteText", FileWriteText, 2);
        vm->DefineNative("FileDelete", FileDelete, 1);
        vm->DefineNative("FileGetSize", FileGetSize, 1);

        // ── Standard Library Phase 3: URL ──
        vm->DefineNative("URLEncode", URLEncode, 1);
        vm->DefineNative("URLDecode", URLDecode, 1);

        // ── Standard Library Phase 4: Math Utilities ──
        vm->DefineNative("MathLog2", MathLog2, 1);
        vm->DefineNative("MathLog1p", MathLog1p, 1);
        vm->DefineNative("MathExpm1", MathExpm1, 1);
        vm->DefineNative("MathAsinh", MathAsinh, 1);
        vm->DefineNative("MathAcosh", MathAcosh, 1);
        vm->DefineNative("MathAtanh", MathAtanh, 1);
        vm->DefineNative("MathClamp01", MathClamp01, 1);
        vm->DefineNative("MathSmoothStep", MathSmoothStep, 3);
        vm->DefineNative("MathPingPong", MathPingPong, 2);
        vm->DefineNative("MathRepeat", MathRepeat, 2);
        vm->DefineNative("MathSumOfSquares", MathSumOfSquares, 1);
        vm->DefineNative("MathDegrees", MathDegrees, 1);
        vm->DefineNative("MathRadians", MathRadians, 1);
        vm->DefineNative("MathNormalizeAngle", MathNormalizeAngle, 1);

        // ── Standard Library Phase 4: Statistical ──
        vm->DefineNative("StatMode", StatMode, 1);
        vm->DefineNative("StatRange", StatRange, 1);
        vm->DefineNative("StatMean", StatMean, 1);
        vm->DefineNative("StatGeometricMean", StatGeometricMean, 1);
        vm->DefineNative("StatHarmonicMean", StatHarmonicMean, 1);
        vm->DefineNative("StatRootMeanSquare", StatRootMeanSquare, 1);

        // ── Standard Library Phase 4: Advanced Color ──
        vm->DefineNative("ColorRGBToHSV", ColorRGBToHSV, 1);
        vm->DefineNative("ColorHSVToRGB", ColorHSVToRGB, 1);
        vm->DefineNative("ColorRGBToHSL", ColorRGBToHSL, 1);
        vm->DefineNative("ColorHSLToRGB", ColorHSLToRGB, 1);
        vm->DefineNative("ColorToGrayscale", ColorToGrayscale, 1);
        vm->DefineNative("ColorDarken", ColorDarken, 2);
        vm->DefineNative("ColorLighten", ColorLighten, 2);
        vm->DefineNative("ColorBlendMultiply", ColorBlendMultiply, 2);
        vm->DefineNative("ColorBlendScreen", ColorBlendScreen, 2);
        vm->DefineNative("ColorBlendOverlay", ColorBlendOverlay, 2);
        vm->DefineNative("ColorDesaturate", ColorDesaturate, 2);
        vm->DefineNative("ColorSaturate", ColorSaturate, 2);

        // ── Standard Library Phase 4: Geometry 3D ──
        vm->DefineNative("GeomDistance3D", GeomDistance3D, 2);
        vm->DefineNative("GeomSphereIntersect", GeomSphereIntersect, 4);
        vm->DefineNative("GeomAABBIntersect3D", GeomAABBIntersect3D, 4);
        vm->DefineNative("GeomPointInSphere", GeomPointInSphere, 3);
        vm->DefineNative("GeomPointInAABB3D", GeomPointInAABB3D, 3);
        vm->DefineNative("GeomTriangleArea3D", GeomTriangleArea3D, 3);
        vm->DefineNative("GeomTriangleNormal", GeomTriangleNormal, 3);
        vm->DefineNative("GeomRaySphereIntersect", GeomRaySphereIntersect, 4);
        vm->DefineNative("GeomRayAABBIntersect", GeomRayAABBIntersect, 4);
        vm->DefineNative("GeomDistanceSquared3D", GeomDistanceSquared3D, 2);

        // ── Standard Library Phase 4: Cryptography & Hashing ──
        vm->DefineNative("HashSHA1Fallback", HashSHA1Fallback, 1);
        vm->DefineNative("HashSHA256Fallback", HashSHA256Fallback, 1);
        vm->DefineNative("HashCRC32", HashCRC32, 1);
        vm->DefineNative("HexEncode", HexEncode, 1);
        vm->DefineNative("HexDecode", HexDecode, 1);
        vm->DefineNative("ROT13", ROT13, 1);

        // ── Standard Library Phase 4: String Extras ──
        vm->DefineNative("StringLeft", StringLeft, 2);
        vm->DefineNative("StringRight", StringRight, 2);
        vm->DefineNative("StringIsWhitespace", StringIsWhitespace, 1);
        vm->DefineNative("StringIsLowerCase", StringIsLowerCase, 1);
        vm->DefineNative("StringIsUpperCase", StringIsUpperCase, 1);
        vm->DefineNative("StringRemovePrefix", StringRemovePrefix, 2);
        vm->DefineNative("StringRemoveSuffix", StringRemoveSuffix, 2);
        vm->DefineNative("StringCountWords", StringCountWords, 1);
        vm->DefineNative("StringToTitleCase", StringToTitleCase, 1);
        vm->DefineNative("StringSwapCase", StringSwapCase, 1);
        vm->DefineNative("StringIsAscii", StringIsAscii, 1);
        vm->DefineNative("StringReverseWords", StringReverseWords, 1);

        // ── Standard Library Phase 4: List Extras ──
        vm->DefineNative("ListChunk", ListChunk, 2);
        vm->DefineNative("ListDifference", ListDifference, 2);
        vm->DefineNative("ListIntersection", ListIntersection, 2);
        vm->DefineNative("ListUnion", ListUnion, 2);
        vm->DefineNative("ListWithout", ListWithout, 2);
        vm->DefineNative("ListCompact", ListCompact, 1);
        vm->DefineNative("ListPad", ListPad, 3);
        vm->DefineNative("ListLastIndexOf", ListLastIndexOf, 2);
        vm->DefineNative("ListDrop", ListDrop, 2);
        vm->DefineNative("ListTake", ListTake, 2);
        vm->DefineNative("ListReverseInPlace", ListReverseInPlace, 1);
        vm->DefineNative("ListRotate", ListRotate, 2);

        // ── Standard Library Phase 4: Map Extras ──
        vm->DefineNative("MapHasValue", MapHasValue, 2);
        vm->DefineNative("MapInvert", MapInvert, 1);
        vm->DefineNative("MapClone", MapClone, 1);
        vm->DefineNative("MapIsEmpty", MapIsEmpty, 1);
        vm->DefineNative("MapDefaultGet", MapDefaultGet, 3);

        // ── Standard Library Phase 4: URL & Path Extras ──
        vm->DefineNative("PathNormalize", PathNormalize, 1);
        vm->DefineNative("PathIsRelative", PathIsRelative, 1);
        vm->DefineNative("URLGetHost", URLGetHost, 1);
        vm->DefineNative("URLGetProtocol", URLGetProtocol, 1);
        vm->DefineNative("URLGetPath", URLGetPath, 1);
        vm->DefineNative("URLGetQuery", URLGetQuery, 1);

        // ── Quaternion Operations ──
        vm->DefineNative("QuatCreate", QuatCreate, 4);
        vm->DefineNative("QuatMultiply", QuatMultiply, 2);
        vm->DefineNative("QuatNormalize", QuatNormalize, 1);
        vm->DefineNative("QuatConjugate", QuatConjugate, 1);
        vm->DefineNative("QuatInverse", QuatInverse, 1);
        vm->DefineNative("QuatFromAxisAngle", QuatFromAxisAngle, 2);
        vm->DefineNative("QuatToEuler", QuatToEuler, 1);
        vm->DefineNative("QuatFromEuler", QuatFromEuler, 1);
        vm->DefineNative("QuatSlerp", QuatSlerp, 3);
        vm->DefineNative("QuatDot", QuatDot, 2);
        vm->DefineNative("QuatLength", QuatLength, 1);
        vm->DefineNative("QuatRotateVec3", QuatRotateVec3, 2);

        // ── Easing Functions ──
        vm->DefineNative("EaseInQuad", EaseInQuad, 1);
        vm->DefineNative("EaseOutQuad", EaseOutQuad, 1);
        vm->DefineNative("EaseInOutQuad", EaseInOutQuad, 1);
        vm->DefineNative("EaseInCubic", EaseInCubic, 1);
        vm->DefineNative("EaseOutCubic", EaseOutCubic, 1);
        vm->DefineNative("EaseInOutCubic", EaseInOutCubic, 1);
        vm->DefineNative("EaseInSine", EaseInSine, 1);
        vm->DefineNative("EaseOutSine", EaseOutSine, 1);
        vm->DefineNative("EaseInOutSine", EaseInOutSine, 1);
        vm->DefineNative("EaseInExpo", EaseInExpo, 1);
        vm->DefineNative("EaseOutExpo", EaseOutExpo, 1);
        vm->DefineNative("EaseInOutExpo", EaseInOutExpo, 1);
        vm->DefineNative("EaseInElastic", EaseInElastic, 1);
        vm->DefineNative("EaseOutElastic", EaseOutElastic, 1);
        vm->DefineNative("EaseInBounce", EaseInBounce, 1);
        vm->DefineNative("EaseOutBounce", EaseOutBounce, 1);

        // ── Noise & Procedural ──
        vm->DefineNative("NoiseHash", NoiseHash, 1);
        vm->DefineNative("NoiseFade", NoiseFade, 1);
        vm->DefineNative("NoiseGrad1D", NoiseGrad1D, 2);
        vm->DefineNative("NoisePerlin1D", NoisePerlin1D, 1);
        vm->DefineNative("NoiseValueNoise2D", NoiseValueNoise2D, 2);
        vm->DefineNative("NoiseFBM1D", NoiseFBM1D, 3);

        // ── Type Checking ──
        vm->DefineNative("IsInt", IsInt, 1);
        vm->DefineNative("IsFloat", IsFloat, 1);
        vm->DefineNative("IsString", IsString, 1);
        vm->DefineNative("IsBool", IsBool, 1);
        vm->DefineNative("IsNull", IsNull, 1);
        vm->DefineNative("IsList", IsList, 1);
        vm->DefineNative("IsMap", IsMap, 1);
        vm->DefineNative("IsNumber", IsNumber, 1);

        // ── Type Conversion ──
        vm->DefineNative("ToInt", ToInt, 1);
        vm->DefineNative("ToFloat", ToFloat, 1);
        vm->DefineNative("ToString", ToString, 1);
        vm->DefineNative("ToBool", ToBool, 1);
        vm->DefineNative("CharToCode", CharToCode, 1);
        vm->DefineNative("CodeToChar", CodeToChar, 1);

        // ── Encoding ──
        vm->DefineNative("EncodeUTF8Length", EncodeUTF8Length, 1);

        // ── String Advanced ──
        vm->DefineNative("StringCenter", StringCenter, 2);
        vm->DefineNative("StringLJust", StringLJust, 2);
        vm->DefineNative("StringRJust", StringRJust, 2);
        vm->DefineNative("StringExpandTabs", StringExpandTabs, 2);
        vm->DefineNative("StringZFill", StringZFill, 2);
        vm->DefineNative("StringIsNumeric", StringIsNumeric, 1);
        vm->DefineNative("StringIsAlphaNumeric", StringIsAlphaNumeric, 1);
        vm->DefineNative("StringSlice", StringSlice, 3);

        // ── List Functional ──
        vm->DefineNative("ListZip", ListZip, 2);
        vm->DefineNative("ListProduct", ListProduct, 1);
        vm->DefineNative("ListCountValue", ListCountValue, 2);

        // ── Map Advanced ──
        vm->DefineNative("MapFromLists", MapFromLists, 2);
        vm->DefineNative("MapToList", MapToList, 1);
        vm->DefineNative("MapFilter", MapFilter, 2);
        vm->DefineNative("MapCount", MapCount, 1);

        // ── Signal & Wave ──
        vm->DefineNative("WaveSine", WaveSine, 2);
        vm->DefineNative("WaveSquare", WaveSquare, 2);
        vm->DefineNative("WaveSawtooth", WaveSawtooth, 2);
        vm->DefineNative("WaveTriangle", WaveTriangle, 2);
        vm->DefineNative("WavePulse", WavePulse, 3);

        // ── Physics Math ──
        vm->DefineNative("PhysMathGravityForce", PhysMathGravityForce, 3);
        vm->DefineNative("PhysMathKineticEnergy", PhysMathKineticEnergy, 2);
        vm->DefineNative("PhysMathProjectileRange", PhysMathProjectileRange, 2);
        vm->DefineNative("PhysMathTerminalVelocity", PhysMathTerminalVelocity, 3);
        vm->DefineNative("PhysMathSpringForce", PhysMathSpringForce, 2);
        vm->DefineNative("PhysMathMomentum", PhysMathMomentum, 2);

        // ── System Info ──
        vm->DefineNative("SysUptime", SysUptime, 0);
        vm->DefineNative("SysPID", SysPID, 0);
        vm->DefineNative("SysThreadID", SysThreadID, 0);

        // ════════════════════════════════════════
        //  STANDARD LIBRARY PHASE 6 EXTENSIONS
        // ════════════════════════════════════════

        // ── Crypto & Hash ──
        vm->DefineNative("CryptoMD5", CryptoMD5, 1);
        vm->DefineNative("CryptoSHA1", CryptoSHA1, 1);
        vm->DefineNative("CryptoSHA256", CryptoSHA256, 1);
        vm->DefineNative("CryptoCRC32", CryptoCRC32, 1);
        vm->DefineNative("CryptoAdler32", CryptoAdler32, 1);
        vm->DefineNative("CryptoHashCombine", CryptoHashCombine, 2);
        
        // ── Date & Time ──
        vm->DefineNative("TimeNow", TimeNow, 0);
        vm->DefineNative("TimeFormatDate", TimeFormatDate, 2);
        vm->DefineNative("TimeIsLeapYear", TimeIsLeapYear, 1);
        vm->DefineNative("TimeDaysInMonth", TimeDaysInMonth, 2);
        vm->DefineNative("TimeAddDays", TimeAddDays, 2);
        vm->DefineNative("TimeDiffSeconds", TimeDiffSeconds, 2);
        
        // ── Matrix Math (4x4) ──
        vm->DefineNative("Mat4Create", Mat4Create, 0);
        vm->DefineNative("Mat4Determinant", Mat4Determinant, 1);
        
        // ── Vector 3D Math ──
        vm->DefineNative("Vec3Create", Vec3Create, 3);
        vm->DefineNative("Vec3Cross", Vec3Cross, 2);
        vm->DefineNative("Vec3Dot", Vec3Dot, 2);
        vm->DefineNative("Vec3Normalize", Vec3Normalize, 1);
        vm->DefineNative("Vec3Distance", Vec3Distance, 2);
        vm->DefineNative("Vec3Project", Vec3Project, 2);
        vm->DefineNative("Vec3Angle", Vec3Angle, 2);
        
        // ── Vector 2D Math ──
        vm->DefineNative("Vec2Create", Vec2Create, 2);
        vm->DefineNative("Vec2Add", Vec2Add, 2);
        vm->DefineNative("Vec2Sub", Vec2Sub, 2);
        vm->DefineNative("Vec2Dot", Vec2Dot, 2);
        vm->DefineNative("Vec2Distance", Vec2Distance, 2);
        vm->DefineNative("Vec2Angle", Vec2Angle, 2);
        vm->DefineNative("Vec2Normalize", Vec2Normalize, 1);
        
        // ── Geometry ──
        vm->DefineNative("GeomPointInPolygon", GeomPointInPolygon, 2);
        vm->DefineNative("GeomLineIntersect2D", GeomLineIntersect2D, 4);
        vm->DefineNative("GeomAABBIntersect2D", GeomAABBIntersect2D, 4);
        vm->DefineNative("GeomClosestPointOnLine", GeomClosestPointOnLine, 3);
        
        // ── File/Path (Virtual) ──
        vm->DefineNative("PathGetDirectory", PathGetDirectory, 1);
        vm->DefineNative("PathChangeExtension", PathChangeExtension, 2);
        vm->DefineNative("FileExistsVirtual", FileExistsVirtual, 1);
        vm->DefineNative("FileGetSizeVirtual", FileGetSizeVirtual, 1);
        
        // ── Random Advanced ──
        vm->DefineNative("RandomGaussian", RandomGaussian, 2);
        vm->DefineNative("RandomRange", RandomRange, 2);
        vm->DefineNative("RandomChoice", RandomChoice, 1);
        vm->DefineNative("RandomShuffleList", RandomShuffleList, 1);
        vm->DefineNative("RandomColor", RandomColor, 0);
        vm->DefineNative("RandomUnitVec2", RandomUnitVec2, 0);
        vm->DefineNative("RandomUnitVec3", RandomUnitVec3, 0);
        vm->DefineNative("NoiseSimplex2D", NoiseSimplex2D, 2);
        
        // ── Color Space ──
        vm->DefineNative("ColorBlend", ColorBlend, 3);
        vm->DefineNative("ColorGrayscale", ColorGrayscale, 3);
        vm->DefineNative("ColorLuminance", ColorLuminance, 3);
        
        // ── Bitwise ──
        vm->DefineNative("BitCount", BitCount, 1);
        vm->DefineNative("BitToggle", BitToggle, 2);
        vm->DefineNative("BitCheck", BitCheck, 2);
        
        // ── Regex/Pattern (Simplified) ──
        vm->DefineNative("PatternMatch", PatternMatch, 2);
        vm->DefineNative("PatternReplace", PatternReplace, 3);
        vm->DefineNative("PatternSearch", PatternSearch, 2);
        vm->DefineNative("PatternExtract", PatternExtract, 2);

        // ── String Utilities 2 ──
        vm->DefineNative("StringTitle", StringTitle, 1);


        // ── Key Map ──
        CQSMapObject* keyMap = new CQSMapObject();
        keyMap->Entries["A"] = Value::MakeInt(65);
        keyMap->Entries["B"] = Value::MakeInt(66);
        keyMap->Entries["C"] = Value::MakeInt(67);
        keyMap->Entries["D"] = Value::MakeInt(68);
        keyMap->Entries["E"] = Value::MakeInt(69);
        keyMap->Entries["F"] = Value::MakeInt(70);
        keyMap->Entries["G"] = Value::MakeInt(71);
        keyMap->Entries["H"] = Value::MakeInt(72);
        keyMap->Entries["I"] = Value::MakeInt(73);
        keyMap->Entries["J"] = Value::MakeInt(74);
        keyMap->Entries["K"] = Value::MakeInt(75);
        keyMap->Entries["L"] = Value::MakeInt(76);
        keyMap->Entries["M"] = Value::MakeInt(77);
        keyMap->Entries["N"] = Value::MakeInt(78);
        keyMap->Entries["O"] = Value::MakeInt(79);
        keyMap->Entries["P"] = Value::MakeInt(80);
        keyMap->Entries["Q"] = Value::MakeInt(81);
        keyMap->Entries["R"] = Value::MakeInt(82);
        keyMap->Entries["S"] = Value::MakeInt(83);
        keyMap->Entries["T"] = Value::MakeInt(84);
        keyMap->Entries["U"] = Value::MakeInt(85);
        keyMap->Entries["V"] = Value::MakeInt(86);
        keyMap->Entries["W"] = Value::MakeInt(87);
        keyMap->Entries["X"] = Value::MakeInt(88);
        keyMap->Entries["Y"] = Value::MakeInt(89);
        keyMap->Entries["Z"] = Value::MakeInt(90);
        keyMap->Entries["Space"] = Value::MakeInt(32);
        keyMap->Entries["Escape"] = Value::MakeInt(256);
        keyMap->Entries["Enter"] = Value::MakeInt(257);
        keyMap->Entries["Tab"] = Value::MakeInt(258);
        keyMap->Entries["Backspace"] = Value::MakeInt(259);
        keyMap->Entries["LeftShift"] = Value::MakeInt(340);
        keyMap->Entries["LeftControl"] = Value::MakeInt(341);
        keyMap->Entries["LeftAlt"] = Value::MakeInt(342);
        keyMap->Entries["RightShift"] = Value::MakeInt(344);
        keyMap->Entries["RightControl"] = Value::MakeInt(345);
        keyMap->Entries["Up"] = Value::MakeInt(265);
        keyMap->Entries["Down"] = Value::MakeInt(264);
        keyMap->Entries["Left"] = Value::MakeInt(263);
        keyMap->Entries["Right"] = Value::MakeInt(262);
        keyMap->Entries["Num0"] = Value::MakeInt(48);
        keyMap->Entries["Num1"] = Value::MakeInt(49);
        keyMap->Entries["Num2"] = Value::MakeInt(50);
        keyMap->Entries["Num3"] = Value::MakeInt(51);
        keyMap->Entries["Num4"] = Value::MakeInt(52);
        keyMap->Entries["Num5"] = Value::MakeInt(53);
        keyMap->Entries["Num6"] = Value::MakeInt(54);
        keyMap->Entries["Num7"] = Value::MakeInt(55);
        keyMap->Entries["Num8"] = Value::MakeInt(56);
        keyMap->Entries["Num9"] = Value::MakeInt(57);
        keyMap->Entries["F1"] = Value::MakeInt(290);
        keyMap->Entries["F2"] = Value::MakeInt(291);
        keyMap->Entries["F3"] = Value::MakeInt(292);
        keyMap->Entries["F4"] = Value::MakeInt(293);
        keyMap->Entries["F5"] = Value::MakeInt(294);
        keyMap->Entries["F6"] = Value::MakeInt(295);
        keyMap->Entries["F7"] = Value::MakeInt(296);
        keyMap->Entries["F8"] = Value::MakeInt(297);
        keyMap->Entries["F9"] = Value::MakeInt(298);
        keyMap->Entries["F10"] = Value::MakeInt(299);
        keyMap->Entries["F11"] = Value::MakeInt(300);
        keyMap->Entries["F12"] = Value::MakeInt(301);
        vm->DefineGlobal("Key", Value::MakeObject(keyMap));

        // Mouse button map
        CQSMapObject* mouseMap = new CQSMapObject();
        mouseMap->Entries["Left"] = Value::MakeInt(0);
        mouseMap->Entries["Right"] = Value::MakeInt(1);
        mouseMap->Entries["Middle"] = Value::MakeInt(2);
        vm->DefineGlobal("Mouse", Value::MakeObject(mouseMap));

        // Math constants
        CQSMapObject* mathMap = new CQSMapObject();
        mathMap->Entries["PI"] = Value::MakeFloat(3.14159265358979323846);
        mathMap->Entries["TAU"] = Value::MakeFloat(6.28318530717958647692);
        mathMap->Entries["E"] = Value::MakeFloat(2.71828182845904523536);
        mathMap->Entries["Infinity"] = Value::MakeFloat(1e30);
        mathMap->Entries["Deg2Rad"] = Value::MakeFloat(0.01745329251);
        mathMap->Entries["Rad2Deg"] = Value::MakeFloat(57.2957795131);
        vm->DefineGlobal("Math", Value::MakeObject(mathMap));

        // ── Phase 7 Bindings ──
        vm->DefineNative("MathSmoothStep", MathSmoothStep, 3);
        vm->DefineNative("MathPingPong", MathPingPong, 2);
        vm->DefineNative("MathRemap", MathRemap, 5);
        vm->DefineNative("MathWrap", MathWrap, 3);
        vm->DefineNative("MathBezierQuad", MathBezierQuad, 4);
        vm->DefineNative("MathBezierCubic", MathBezierCubic, 5);
        vm->DefineNative("MathCatmullRom", MathCatmullRom, 5);
        vm->DefineNative("MathHermite", MathHermite, 5);
        vm->DefineNative("Base64Encode", Base64Encode, 1);
        vm->DefineNative("Base64Decode", Base64Decode, 1);
        vm->DefineNative("URLEncode", URLEncode, 1);
        vm->DefineNative("URLDecode", URLDecode, 1);
        vm->DefineNative("HexEncode", HexEncode, 1);
        vm->DefineNative("HexDecode", HexDecode, 1);
        vm->DefineNative("Rot13", Rot13, 1);
        vm->DefineNative("SetUnion", SetUnion, 2);
        vm->DefineNative("SetIntersection", SetIntersection, 2);
        vm->DefineNative("SetDifference", SetDifference, 2);
        vm->DefineNative("SetIsSubset", SetIsSubset, 2);
        vm->DefineNative("SetIsSuperset", SetIsSuperset, 2);
        vm->DefineNative("ListPushFront", ListPushFront, 2);
        vm->DefineNative("ListPopFront", ListPopFront, 1);
        vm->DefineNative("ListPopBack", ListPopBack, 1);
        vm->DefineNative("ListInsertAt", ListInsertAt, 3);
        vm->DefineNative("ListRemoveAt", ListRemoveAt, 2);
        vm->DefineNative("ListSortAscending", ListSortAscending, 1);
        vm->DefineNative("ListSortDescending", ListSortDescending, 1);
        vm->DefineNative("ListFill", ListFill, 2);
        vm->DefineNative("ListChunk", ListChunk, 2);
        vm->DefineNative("StringPadLeft", StringPadLeft, 3);
        vm->DefineNative("StringPadRight", StringPadRight, 3);
        vm->DefineNative("StringRepeat", StringRepeat, 2);
        vm->DefineNative("StringReverse", StringReverse, 1);
        vm->DefineNative("StringTrimLeft", StringTrimLeft, 1);
        vm->DefineNative("StringTrimRight", StringTrimRight, 1);
        vm->DefineNative("StringSplit", StringSplit, 2);
        vm->DefineNative("StringJoin", StringJoin, 2);
        vm->DefineNative("ColorToCMYK", ColorToCMYK, 3);
        vm->DefineNative("ColorFromCMYK", ColorFromCMYK, 4);
        vm->DefineNative("ColorToHSL", ColorToHSL, 3);
        vm->DefineNative("ColorFromHSL", ColorFromHSL, 3);
        vm->DefineNative("ColorInvert", ColorInvert, 3);
        vm->DefineNative("ColorLighten", ColorLighten, 4);
        vm->DefineNative("ColorDarken", ColorDarken, 4);
        vm->DefineNative("BinaryToDecimal", BinaryToDecimal, 1);
        vm->DefineNative("DecimalToBinary", DecimalToBinary, 1);
        vm->DefineNative("HexToDecimal", HexToDecimal, 1);
        vm->DefineNative("DecimalToHex", DecimalToHex, 1);
        vm->DefineNative("Mat3Create", Mat3Create, 0);
        vm->DefineNative("Mat3Identity", Mat3Identity, 0);
        vm->DefineNative("Mat3Determinant", Mat3Determinant, 1);
        vm->DefineNative("Mat3Transpose", Mat3Transpose, 1);
        vm->DefineNative("Mat3Multiply", Mat3Multiply, 2);
        vm->DefineNative("QuatCreate", QuatCreate, 4);
        vm->DefineNative("QuatIdentity", QuatIdentity, 0);
        vm->DefineNative("QuatMultiply", QuatMultiply, 2);
        vm->DefineNative("QuatInverse", QuatInverse, 1);
        vm->DefineNative("QuatNormalize", QuatNormalize, 1);
        vm->DefineNative("QuatSlerp", QuatSlerp, 3);
        vm->DefineNative("QuatToEuler", QuatToEuler, 1);
        vm->DefineNative("QuatFromEuler", QuatFromEuler, 3);

        // ── Phase 8 ──
        vm->DefineNative("JSONStringify", JSONStringify, 1);
        vm->DefineNative("JSONParse", JSONParse, 1);
        vm->DefineNative("RegexMatch", RegexMatch, 2);
        vm->DefineNative("CompressRLE", CompressRLE, 1);
        vm->DefineNative("DecompressRLE", DecompressRLE, 1);
        vm->DefineNative("CompressLZ77", CompressLZ77, 1);
        vm->DefineNative("DecompressLZ77", DecompressLZ77, 1);
        vm->DefineNative("MathPrimeFactors", MathPrimeFactors, 1);
        vm->DefineNative("DSPConvolve1D", DSPConvolve1D, 2);
        // ── Phase 9 ──
        vm->DefineNative("NetHttpRequest", NetHttpRequest, 3);
        vm->DefineNative("NetWebSocketConnect", NetWebSocketConnect, 1);
        vm->DefineNative("NetWebSocketSend", NetWebSocketSend, 2);
        vm->DefineNative("NetWebSocketReceive", NetWebSocketReceive, 1);
        
        vm->DefineNative("BufferCreate", BufferCreate, 1);
        vm->DefineNative("BufferWriteInt8", BufferWriteInt8, 3);
        vm->DefineNative("BufferReadInt8", BufferReadInt8, 2);
        vm->DefineNative("BufferWriteInt32", BufferWriteInt32, 3);
        vm->DefineNative("BufferReadInt32", BufferReadInt32, 2);
        vm->DefineNative("BufferWriteFloat", BufferWriteFloat, 3);
        vm->DefineNative("BufferReadFloat", BufferReadFloat, 2);
        vm->DefineNative("BufferWriteString", BufferWriteString, 3);
        vm->DefineNative("BufferReadString", BufferReadString, 3);
        vm->DefineNative("BufferToBase64", BufferToBase64, 1);
        vm->DefineNative("BufferFromBase64", BufferFromBase64, 1);
        
        vm->DefineNative("MLKMeansCluster", MLKMeansCluster, 2);
        vm->DefineNative("MLLinearRegression", MLLinearRegression, 2);
        vm->DefineNative("MLCalculateMean", MLCalculateMean, 1);
        vm->DefineNative("MLCalculateVariance", MLCalculateVariance, 1);
        vm->DefineNative("MLCalculateStandardDeviation", MLCalculateStandardDeviation, 1);
        
        vm->DefineNative("GeomBezierPoint2D", GeomBezierPoint2D, 5);
        vm->DefineNative("GeomCatmullRomPoint2D", GeomCatmullRomPoint2D, 6);
        vm->DefineNative("GeomPolygonCentroid", GeomPolygonCentroid, 1);
        
        vm->DefineNative("GeomBezierPoint3D", GeomBezierPoint3D, 5);
        vm->DefineNative("GeomCatmullRomPoint3D", GeomCatmullRomPoint3D, 6);
        vm->DefineNative("GeomClosestPointOnTriangle3D", GeomClosestPointOnTriangle3D, 4);

        vm->DefineNative("StateSaveGame", StateSaveGame, 2);
        vm->DefineNative("StateLoadGame", StateLoadGame, 1);

        // ── Phase 10 ──
        vm->DefineNative("ImageCreate", ImageCreate, 2);
        vm->DefineNative("ImageSetPixel", ImageSetPixel, 7);
        vm->DefineNative("ImageGetPixel", ImageGetPixel, 3);
        vm->DefineNative("ImageDrawLine", ImageDrawLine, 9);
        vm->DefineNative("ImageDrawCircle", ImageDrawCircle, 8);
        vm->DefineNative("ImageBoxBlur", ImageBoxBlur, 2);

        vm->DefineNative("GeomConvexHull2D", GeomConvexHull2D, 1);
        vm->DefineNative("GeomEarClipTriangulate", GeomEarClipTriangulate, 1);
        vm->DefineNative("GeomLineIntersection2D", GeomLineIntersection2D, 4);

        vm->DefineNative("MathEvalExpression", MathEvalExpression, 2);

        // ── Phase 11 ──
        vm->DefineNative("PhysMathExplicitEuler", PhysMathExplicitEuler, 4);
        vm->DefineNative("PhysMathSemiImplicitEuler", PhysMathSemiImplicitEuler, 4);
        vm->DefineNative("PhysMathVelocityVerlet", PhysMathVelocityVerlet, 4);
        vm->DefineNative("PhysMathRK4", PhysMathRK4, 4);
        vm->DefineNative("PhysMathComputeDragForce", PhysMathComputeDragForce, 4);
        vm->DefineNative("PhysMathComputeMagnusForce", PhysMathComputeMagnusForce, 5);
        vm->DefineNative("PhysMathComputeBuoyancyForce", PhysMathComputeBuoyancyForce, 3);
        vm->DefineNative("PhysMathResolveElasticCollision", PhysMathResolveElasticCollision, 6);
        vm->DefineNative("PhysMathSolveDistanceConstraint", PhysMathSolveDistanceConstraint, 5);
        vm->DefineNative("TransformGetLocalMatrix", TransformGetLocalMatrix, 3);
        vm->DefineNative("TransformHierarchyCalcGlobal", TransformHierarchyCalcGlobal, 2);
        vm->DefineNative("TransformPoint", TransformPoint, 2);
        vm->DefineNative("GeomRayCircleIntersect2D", GeomRayCircleIntersect2D, 4);
        vm->DefineNative("GeomTriangleTriangleIntersect3D", GeomTriangleTriangleIntersect3D, 6);
        vm->DefineNative("BSplineInterpolate", BSplineInterpolate, 3);
        vm->DefineNative("HermiteTangent", HermiteTangent, 5);

        vm->DefineNative("CryptoRC4", CryptoRC4, 2);
        vm->DefineNative("CryptoBase32Encode", CryptoBase32Encode, 1);
        vm->DefineNative("CryptoBase32Decode", CryptoBase32Decode, 1);
        // New math extensions
        vm->DefineNative("ComplexAdd", ComplexAdd, 2);
        vm->DefineNative("ComplexExp", ComplexExp, 1);
        vm->DefineNative("DualQuatFromTR", DualQuatFromTR, 2);
        vm->DefineNative("SamplePoissonDisk2D", SamplePoissonDisk2D, 2);
        vm->DefineNative("RGBtoOKLAB", RGBtoOKLAB, 1);
        
        // ── Phase 12: Calculus & Advanced Math ──
        vm->DefineNative("LinearAlgebraDeterminant", LinearAlgebraDeterminant, 1);
        vm->DefineNative("CompGeomPolygonArea3D", CompGeomPolygonArea3D, 1);
        vm->DefineNative("SurfaceBezier", SurfaceBezier, 3);
        vm->DefineNative("SignalWindowHamming", SignalWindowHamming, 1);

        // ── Phase 13: Advanced Subsystems (Kinematics, Topology, Stats, Num, Fractals) ──
        vm->DefineNative("MathSolveFABRIK", MathSolveFABRIK, 3);
        vm->DefineNative("MathTopologyEuler", MathTopologyEuler, 3);
        vm->DefineNative("MathGradientDescent", MathGradientDescent, 1);
        vm->DefineNative("MathStatNormalPDF", MathStatNormalPDF, 3);
        vm->DefineNative("MathFractalMandelbrot", MathFractalMandelbrot, 3);

        vm->DefineNative("CryptoBase32Encode", CryptoBase32Encode, 1);
        vm->DefineNative("CryptoBase32Decode", CryptoBase32Decode, 1);

        CQ_CORE_INFO("[CQS] NativeBridge: All systems registered.");
    }

    // ════════════════════════════════════════
    //  LOG
    // ════════════════════════════════════════
    Value CQSNativeBridge::LogInfo(int argCount, Value* args)
    {
        if (argCount > 0) CQ_CORE_INFO("[Script] {0}", args[0].ToString());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogWarn(int argCount, Value* args)
    {
        if (argCount > 0) CQ_CORE_WARN("[Script] {0}", args[0].ToString());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogError(int argCount, Value* args)
    {
        if (argCount > 0) CQ_CORE_ERROR("[Script] {0}", args[0].ToString());
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  TRANSFORM
    // ════════════════════════════════════════
    Value CQSNativeBridge::GetPosition(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        return Value::MakeObject(MakeVec3Map(t.Position.x, t.Position.y, t.Position.z));
    }
    Value CQSNativeBridge::SetPosition(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        t.Position = glm::vec3((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::GetRotation(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        return Value::MakeObject(MakeVec3Map(glm::degrees(t.Rotation.x), glm::degrees(t.Rotation.y), glm::degrees(t.Rotation.z)));
    }
    Value CQSNativeBridge::SetRotation(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        t.Rotation = glm::vec3(glm::radians((float)args[0].ToNumber()), glm::radians((float)args[1].ToNumber()), glm::radians((float)args[2].ToNumber()));
        return Value::MakeNull();
    }
    Value CQSNativeBridge::GetScale(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        return Value::MakeObject(MakeVec3Map(t.Scale.x, t.Scale.y, t.Scale.z));
    }
    Value CQSNativeBridge::SetScale(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        t.Scale = glm::vec3((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::GetForward(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        glm::vec3 fwd = Math::GetForwardVector(t.Rotation);
        return Value::MakeObject(MakeVec3Map(fwd.x, fwd.y, fwd.z));
    }
    Value CQSNativeBridge::GetRight(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        glm::vec3 r = Math::GetRightVector(t.Rotation);
        return Value::MakeObject(MakeVec3Map(r.x, r.y, r.z));
    }
    Value CQSNativeBridge::GetUp(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        glm::vec3 u = Math::GetUpVector(t.Rotation);
        return Value::MakeObject(MakeVec3Map(u.x, u.y, u.z));
    }

    // ════════════════════════════════════════
    //  PHYSICS 3D
    // ════════════════════════════════════════
    Value CQSNativeBridge::GetLinearVelocity(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        glm::vec3 v = Physics::GetLinearVelocity(e);
        return Value::MakeObject(MakeVec3Map(v.x, v.y, v.z));
    }
    Value CQSNativeBridge::SetLinearVelocity(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::SetLinearVelocity(e, glm::vec3((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber()));
        return Value::MakeNull();
    }
    Value CQSNativeBridge::GetAngularVelocity(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        glm::vec3 v = Physics::GetAngularVelocity(e);
        return Value::MakeObject(MakeVec3Map(v.x, v.y, v.z));
    }
    Value CQSNativeBridge::SetAngularVelocity(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::SetAngularVelocity(e, glm::vec3((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber()));
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ApplyForce(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::ApplyForce(e, glm::vec3((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber()));
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ApplyImpulse(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::ApplyImpulse(e, glm::vec3((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber()));
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ApplyTorque(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::ApplyTorque(e, glm::vec3((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber()));
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  PHYSICS 2D
    // ════════════════════════════════════════
    Value CQSNativeBridge::GetLinearVelocity2D(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        glm::vec2 v = Physics::GetLinearVelocity2D(e);
        return Value::MakeObject(MakeVec2Map(v.x, v.y));
    }
    Value CQSNativeBridge::SetLinearVelocity2D(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::SetLinearVelocity2D(e, glm::vec2((float)args[0].ToNumber(), (float)args[1].ToNumber()));
        return Value::MakeNull();
    }
    Value CQSNativeBridge::GetAngularVelocity2D(int, Value*)
    {
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        return Value::MakeFloat(Physics::GetAngularVelocity2D(e));
    }
    Value CQSNativeBridge::SetAngularVelocity2D(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::SetAngularVelocity2D(e, (float)args[0].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ApplyForce2D(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::ApplyForceToCenter2D(e, glm::vec2((float)args[0].ToNumber(), (float)args[1].ToNumber()));
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ApplyImpulse2D(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        Entity e = GetCurrentEntity(); if (!e) return Value::MakeNull();
        Physics::ApplyImpulseToCenter2D(e, glm::vec2((float)args[0].ToNumber(), (float)args[1].ToNumber()));
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY: STRING
    // ════════════════════════════════════════
    Value CQSNativeBridge::StringLength(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt((int64_t)args[0].AsString()->Value.length());
    }
    Value CQSNativeBridge::StringSubstring(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsString() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeNull();
        return Value::MakeString(StringUtils::Substring(args[0].AsString()->Value, (size_t)args[1].ToNumber(), (size_t)args[2].ToNumber()));
    }
    Value CQSNativeBridge::StringToUpper(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(StringUtils::ToUpper(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringToLower(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(StringUtils::ToLower(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringIndexOf(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeInt(-1);
        return Value::MakeInt(StringUtils::IndexOf(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::StringReplace(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsString() || !args[1].IsString() || !args[2].IsString()) return Value::MakeNull();
        std::string str = args[0].AsString()->Value;
        std::string search = args[1].AsString()->Value;
        std::string replace = args[2].AsString()->Value;
        if (search.empty()) return Value::MakeString(str);
        return Value::MakeString(StringUtils::ReplaceAll(str, search, replace));
    }
    Value CQSNativeBridge::StringSplit(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeNull();
        auto* list = new CQSListObject();
        auto parts = StringUtils::Split(args[0].AsString()->Value, args[1].AsString()->Value);
        for (const auto& part : parts) list->Elements.push_back(Value::MakeString(part));
        return Value::MakeObject(list);
    }
    Value CQSNativeBridge::StringTrim(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(StringUtils::Trim(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringStartsWith(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::StartsWith(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::StringEndsWith(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::EndsWith(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::StringContains(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::Contains(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::StringPadLeft(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsString() || !args[1].IsNumber() || !args[2].IsString()) return Value::MakeNull();
        std::string padStr = args[2].AsString()->Value;
        char padChar = padStr.empty() ? ' ' : padStr[0];
        return Value::MakeString(StringUtils::PadLeft(args[0].AsString()->Value, (size_t)args[1].ToNumber(), padChar));
    }
    Value CQSNativeBridge::StringPadRight(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsString() || !args[1].IsNumber() || !args[2].IsString()) return Value::MakeNull();
        std::string padStr = args[2].AsString()->Value;
        char padChar = padStr.empty() ? ' ' : padStr[0];
        return Value::MakeString(StringUtils::PadRight(args[0].AsString()->Value, (size_t)args[1].ToNumber(), padChar));
    }
    Value CQSNativeBridge::StringIsAlpha(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsAlpha(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringIsDigit(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsDigit(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringIsAlnum(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsAlnum(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringReverse(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(StringUtils::Reverse(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY: LIST
    // ════════════════════════════════════════
    Value CQSNativeBridge::ListAdd(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList()) return Value::MakeNull();
        args[0].AsList()->Elements.push_back(args[1]);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ListRemoveAt(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        auto* list = args[0].AsList();
        size_t index = (size_t)args[1].ToNumber();
        if (index < list->Elements.size()) {
            Value removed = list->Elements[index];
            list->Elements.erase(list->Elements.begin() + index);
            return removed;
        }
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ListInsert(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        auto* list = args[0].AsList();
        size_t index = (size_t)args[1].ToNumber();
        if (index <= list->Elements.size()) {
            list->Elements.insert(list->Elements.begin() + index, args[2]);
            return Value::MakeBool(true);
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::ListClear(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        args[0].AsList()->Elements.clear();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ListSize(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeInt(0);
        return Value::MakeInt((int64_t)args[0].AsList()->Elements.size());
    }
    Value CQSNativeBridge::ListContains(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList()) return Value::MakeBool(false);
        auto* list = args[0].AsList();
        return Value::MakeBool(ListUtils::Contains(list->Elements, args[1]));
    }
    Value CQSNativeBridge::ListIndexOf(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList()) return Value::MakeInt(-1);
        auto* list = args[0].AsList();
        return Value::MakeInt(ListUtils::IndexOf(list->Elements, args[1]));
    }
    Value CQSNativeBridge::ListJoin(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsString()) return Value::MakeNull();
        std::string result = ListUtils::Join(args[0].AsList()->Elements, args[1].AsString()->Value, [](const Value& v) { return v.ToString(); });
        return Value::MakeString(result);
    }
    Value CQSNativeBridge::ListReverse(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* list = args[0].AsList();
        ListUtils::Reverse(list->Elements);
        return args[0];
    }
    Value CQSNativeBridge::ListShuffle(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* list = args[0].AsList();
        ListUtils::Shuffle(list->Elements);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ListSwap(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsList() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeNull();
        auto* list = args[0].AsList();
        size_t idx1 = (size_t)args[1].ToNumber();
        size_t idx2 = (size_t)args[2].ToNumber();
        return Value::MakeBool(ListUtils::Swap(list->Elements, idx1, idx2));
    }
    Value CQSNativeBridge::ListCount(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList()) return Value::MakeInt(0);
        auto* list = args[0].AsList();
        return Value::MakeInt(ListUtils::Count(list->Elements, args[1]));
    }
    Value CQSNativeBridge::ListMin(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        double minVal;
        bool found = ListUtils::MinNumeric(args[0].AsList()->Elements, [](const Value& v, double& out) {
            if (v.IsNumber()) { out = v.ToNumber(); return true; } return false;
        }, minVal);
        return found ? Value::MakeFloat(minVal) : Value::MakeNull();
    }
    Value CQSNativeBridge::ListMax(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        double maxVal;
        bool found = ListUtils::MaxNumeric(args[0].AsList()->Elements, [](const Value& v, double& out) {
            if (v.IsNumber()) { out = v.ToNumber(); return true; } return false;
        }, maxVal);
        return found ? Value::MakeFloat(maxVal) : Value::MakeNull();
    }
    Value CQSNativeBridge::ListSum(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        double sum = ListUtils::SumNumeric(args[0].AsList()->Elements, [](const Value& v, double& out) {
            if (v.IsNumber()) { out = v.ToNumber(); return true; } return false;
        });
        return Value::MakeFloat(sum);
    }
    Value CQSNativeBridge::ListAverage(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        double avg;
        bool found = ListUtils::AverageNumeric(args[0].AsList()->Elements, [](const Value& v, double& out) {
            if (v.IsNumber()) { out = v.ToNumber(); return true; } return false;
        }, avg);
        return found ? Value::MakeFloat(avg) : Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY: MAP
    // ════════════════════════════════════════
    Value CQSNativeBridge::MapSet(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsString()) return Value::MakeNull();
        args[0].AsMap()->Entries[args[1].AsString()->Value] = args[2];
        return Value::MakeNull();
    }
    Value CQSNativeBridge::MapGet(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsString()) return Value::MakeNull();
        auto* map = args[0].AsMap();
        std::string key = args[1].AsString()->Value;
        auto it = map->Entries.find(key);
        if (it != map->Entries.end()) return it->second;
        return Value::MakeNull();
    }
    Value CQSNativeBridge::MapHasKey(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsString()) return Value::MakeBool(false);
        auto* map = args[0].AsMap();
        return Value::MakeBool(map->Entries.find(args[1].AsString()->Value) != map->Entries.end());
    }
    Value CQSNativeBridge::MapRemove(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsString()) return Value::MakeBool(false);
        auto* map = args[0].AsMap();
        return Value::MakeBool(map->Entries.erase(args[1].AsString()->Value) > 0);
    }
    Value CQSNativeBridge::MapKeys(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto keys = MapUtils::GetKeys(args[0].AsMap()->Entries);
        auto* list = new CQSListObject();
        for (const auto& key : keys) list->Elements.push_back(Value::MakeString(key));
        return Value::MakeObject(list);
    }
    Value CQSNativeBridge::MapValues(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto values = MapUtils::GetValues(args[0].AsMap()->Entries);
        auto* list = new CQSListObject();
        for (const auto& val : values) list->Elements.push_back(val);
        return Value::MakeObject(list);
    }
    Value CQSNativeBridge::MapSize(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeInt(0);
        return Value::MakeInt((int64_t)args[0].AsMap()->Entries.size());
    }
    Value CQSNativeBridge::MapClear(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        args[0].AsMap()->Entries.clear();
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY: EXTENDED MATH
    // ════════════════════════════════════════
    Value CQSNativeBridge::MathRandomInt(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        int64_t minVal = (int64_t)args[0].ToNumber();
        int64_t maxVal = (int64_t)args[1].ToNumber();
        if (maxVal <= minVal) return Value::MakeInt(minVal);
        return Value::MakeInt(Math::Random::Int((int)minVal, (int)maxVal));
    }
    Value CQSNativeBridge::MathRandomRange(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Random::Float((float)args[0].ToNumber(), (float)args[1].ToNumber()));
    }
    Value CQSNativeBridge::MathSign(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        double val = args[0].ToNumber();
        if (val > 0.0) return Value::MakeInt(1);
        if (val < 0.0) return Value::MakeInt(-1);
        return Value::MakeInt(0);
    }
    Value CQSNativeBridge::MathRound(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::round(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathTrunc(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::trunc(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathLog(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::log(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathLog10(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::log10(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathExp(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::exp(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathAsin(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::asin(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathAcos(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::acos(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathAtan(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::atan(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathAtan2(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::atan2(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::MathSinh(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::sinh(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathCosh(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::cosh(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathTanh(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::tanh(args[0].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY: CRYPTO & ENCODE
    // ════════════════════════════════════════
    Value CQSNativeBridge::Base64Encode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(Crypto::Base64Encode(args[0].AsString()->Value));
    }

    Value CQSNativeBridge::Base64Decode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(Crypto::Base64Decode(args[0].AsString()->Value));
    }

    Value CQSNativeBridge::HashMD5Fallback(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(Crypto::MD5(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: STRING EXTRAS
    // ════════════════════════════════════════
    Value CQSNativeBridge::StringCount(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeInt(0);
        return Value::MakeInt(StringUtils::Count(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::StringRepeat(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeNull();
        return Value::MakeString(StringUtils::Repeat(args[0].AsString()->Value, (int)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringCapitalize(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(StringUtils::Capitalize(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringCharAt(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::CharAt(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringCharCodeAt(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(StringUtils::CharCodeAt(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringFromCharCode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::FromCharCode((int)args[0].ToNumber()));
    }
    Value CQSNativeBridge::StringTrimLeft(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(StringUtils::TrimLeft(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringTrimRight(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return Value::MakeString(StringUtils::TrimRight(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringLastIndexOf(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeInt(-1);
        return Value::MakeInt(StringUtils::LastIndexOf(args[0].AsString()->Value, args[1].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: LIST EXTRAS
    // ════════════════════════════════════════
    Value CQSNativeBridge::ListSort(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* list = args[0].AsList();
        ListUtils::Sort(list->Elements, [](const Value& a, const Value& b) {
            if (a.IsNumber() && b.IsNumber()) return a.ToNumber() < b.ToNumber();
            return a.ToString() < b.ToString();
        });
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ListSlice(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsList() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeNull();
        auto* src = args[0].AsList();
        auto* result = new CQSListObject();
        result->Elements = ListUtils::Slice(src->Elements, (int)args[1].ToNumber(), (int)args[2].ToNumber());
        return Value::MakeObject(result);
    }
    Value CQSNativeBridge::ListFlatten(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* src = args[0].AsList();
        std::vector<std::vector<Value>> nested;
        for (auto& elem : src->Elements) {
            if (elem.IsList()) nested.push_back(elem.AsList()->Elements);
            else nested.push_back({elem});
        }
        auto* result = new CQSListObject();
        result->Elements = ListUtils::Flatten(nested);
        return Value::MakeObject(result);
    }
    Value CQSNativeBridge::ListUnique(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* src = args[0].AsList();
        auto* result = new CQSListObject();
        result->Elements = ListUtils::Unique(src->Elements, [](const Value& v) { return v.ToString(); });
        return Value::MakeObject(result);
    }
    Value CQSNativeBridge::ListFill(int argCount, Value* args)
    {
        if (argCount < 2 || !args[1].IsNumber()) return Value::MakeNull();
        auto* list = new CQSListObject();
        list->Elements = ListUtils::Fill(args[0], (int)args[1].ToNumber());
        return Value::MakeObject(list);
    }
    Value CQSNativeBridge::ListRepeat(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        auto* result = new CQSListObject();
        result->Elements = ListUtils::Repeat(args[0].AsList()->Elements, (int)args[1].ToNumber());
        return Value::MakeObject(result);
    }
    Value CQSNativeBridge::ListPop(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        Value outVal = Value::MakeNull();
        ListUtils::Pop(args[0].AsList()->Elements, outVal);
        return outVal;
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: MAP EXTRAS
    // ════════════════════════════════════════
    Value CQSNativeBridge::MapMerge(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        auto* dst = args[0].AsMap();
        auto* src = args[1].AsMap();
        MapUtils::Merge(dst->Entries, src->Entries);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::MapEntries(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* list = new CQSListObject();
        for (auto& pair : args[0].AsMap()->Entries) {
            auto* entry = new CQSListObject();
            entry->Elements.push_back(Value::MakeString(pair.first));
            entry->Elements.push_back(pair.second);
            list->Elements.push_back(Value::MakeObject(entry));
        }
        return Value::MakeObject(list);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: EXTENDED MATH
    // ════════════════════════════════════════
    Value CQSNativeBridge::MathVariance(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0.0);
        auto* list = args[0].AsList();
        std::vector<double> vals;
        for (auto& e : list->Elements) if (e.IsNumber()) vals.push_back(e.ToNumber());
        return Value::MakeFloat(Math::Variance(vals));
    }
    Value CQSNativeBridge::MathStandardDeviation(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0.0);
        auto* list = args[0].AsList();
        std::vector<double> vals;
        for (auto& e : list->Elements) if (e.IsNumber()) vals.push_back(e.ToNumber());
        return Value::MakeFloat(Math::StandardDeviation(vals));
    }
    Value CQSNativeBridge::MathMedian(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0.0);
        auto* list = args[0].AsList();
        std::vector<double> vals;
        for (auto& e : list->Elements) if (e.IsNumber()) vals.push_back(e.ToNumber());
        return Value::MakeFloat(Math::Median(vals));
    }
    Value CQSNativeBridge::MathFactorial(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(1);
        return Value::MakeInt(Math::Factorial((int64_t)args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathIsPrime(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeBool(false);
        return Value::MakeBool(Math::IsPrime((int64_t)args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathGCD(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::GCD((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::MathLCM(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::LCM((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::MathDegToRad(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::DegToRad((float)args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathRadToDeg(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::RadToDeg((float)args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathFmod(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::fmod(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::MathHypot(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::hypot(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::MathCbrt(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::cbrt(args[0].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: DATE / TIME
    // ════════════════════════════════════════
    Value CQSNativeBridge::DateNow(int, Value*)
    {
        return Value::MakeFloat(DateUtils::NowMs());
    }
    Value CQSNativeBridge::DateGetYear(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::GetYear(args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateGetMonth(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::GetMonth(args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateGetDay(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::GetDay(args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateGetHours(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::GetHours(args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateGetMinutes(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::GetMinutes(args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateGetSeconds(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::GetSeconds(args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateFormat(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsString()) return Value::MakeString("");
        return Value::MakeString(DateUtils::Format(args[0].ToNumber(), args[1].AsString()->Value));
    }
    Value CQSNativeBridge::DateGetDayOfWeek(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::GetDayOfWeek(args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateGetDayOfYear(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::GetDayOfYear(args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateIsLeapYear(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeBool(false);
        return Value::MakeBool(DateUtils::IsLeapYear((int)args[0].ToNumber()));
    }
    Value CQSNativeBridge::DateDiffSeconds(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(DateUtils::DiffSeconds(args[0].ToNumber(), args[1].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: PATH UTILITIES
    // ════════════════════════════════════════
    Value CQSNativeBridge::PathGetExtension(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(FileSystem::GetExtension(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::PathGetFileName(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(FileSystem::GetFileName(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::PathGetFileNameWithoutExtension(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(FileSystem::GetFileNameWithoutExtension(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::PathGetDirectoryName(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(FileSystem::GetDirectoryPath(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::PathCombine(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeString("");
        return Value::MakeString(FileSystem::CombinePaths(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::PathIsAbsolute(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(FileSystem::IsAbsolutePath(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: COLOR UTILITIES
    // ════════════════════════════════════════
    Value CQSNativeBridge::ColorHexToRGB(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        float r, g, b;
        Math::HexToRGB(args[0].AsString()->Value, r, g, b);
        auto* m = new CQSMapObject();
        m->Entries["r"] = Value::MakeFloat(r);
        m->Entries["g"] = Value::MakeFloat(g);
        m->Entries["b"] = Value::MakeFloat(b);
        return Value::MakeObject(m);
    }
    Value CQSNativeBridge::ColorRGBToHex(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeString("#000000");
        return Value::MakeString(Math::RGBToHex(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber()));
    }
    Value CQSNativeBridge::ColorLerp(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsNumber()) return Value::MakeNull();
        float t = (float)args[2].ToNumber();
        auto* a = args[0].AsMap(); auto* b = args[1].AsMap();
        Math::CQVec3 ca(
            a->Entries.count("r") ? a->Entries["r"].ToNumber() : 0.0f,
            a->Entries.count("g") ? a->Entries["g"].ToNumber() : 0.0f,
            a->Entries.count("b") ? a->Entries["b"].ToNumber() : 0.0f
        );
        Math::CQVec3 cb(
            b->Entries.count("r") ? b->Entries["r"].ToNumber() : 0.0f,
            b->Entries.count("g") ? b->Entries["g"].ToNumber() : 0.0f,
            b->Entries.count("b") ? b->Entries["b"].ToNumber() : 0.0f
        );
        Math::CQVec3 c = Math::Vec3Lerp(ca, cb, t);
        auto* m = new CQSMapObject();
        m->Entries["r"] = Value::MakeFloat(c.x);
        m->Entries["g"] = Value::MakeFloat(c.y);
        m->Entries["b"] = Value::MakeFloat(c.z);
        return Value::MakeObject(m);
    }
    Value CQSNativeBridge::ColorInvert(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeNull();
        float rOut, gOut, bOut;
        Math::ColorInvert((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber(), rOut, gOut, bOut);
        auto* m = new CQSMapObject();
        m->Entries["r"] = Value::MakeFloat(rOut);
        m->Entries["g"] = Value::MakeFloat(gOut);
        m->Entries["b"] = Value::MakeFloat(bOut);
        return Value::MakeObject(m);
    }
    Value CQSNativeBridge::ColorBrightness(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Brightness(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: GEOMETRY
    // ════════════════════════════════════════
    Value CQSNativeBridge::GeomDistance2D(int argCount, Value* args)
    {
        if (argCount < 4) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Distance2D(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber()));
    }
    Value CQSNativeBridge::GeomCircleIntersect(int argCount, Value* args)
    {
        if (argCount < 6) return Value::MakeBool(false);
        return Value::MakeBool(Math::CircleIntersect(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber(), args[4].ToNumber(), args[5].ToNumber()));
    }
    Value CQSNativeBridge::GeomAABBIntersect(int argCount, Value* args)
    {
        if (argCount < 8) return Value::MakeBool(false);
        return Value::MakeBool(Math::AABBIntersect(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber(), args[4].ToNumber(), args[5].ToNumber(), args[6].ToNumber(), args[7].ToNumber()));
    }
    Value CQSNativeBridge::GeomPolygonArea(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0.0);
        auto* pts = args[0].AsList();
        std::vector<glm::vec2> points;
        for (auto& p : pts->Elements) {
            if (p.IsMap()) {
                auto* m = p.AsMap();
                points.push_back(glm::vec2(
                    m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f,
                    m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f
                ));
            }
        }
        return Value::MakeFloat(Math::PolygonArea2D(points));
    }
    Value CQSNativeBridge::GeomPointInRect(int argCount, Value* args)
    {
        if (argCount < 6) return Value::MakeBool(false);
        return Value::MakeBool(Math::PointInRect(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber(), args[4].ToNumber(), args[5].ToNumber()));
    }
    Value CQSNativeBridge::GeomAngleBetween(int argCount, Value* args)
    {
        if (argCount < 4) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::AngleBetween(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: REGEX
    // ════════════════════════════════════════
    Value CQSNativeBridge::RegexIsMatch(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(RegexUtils::Search(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::RegexReplace(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsString() || !args[1].IsString() || !args[2].IsString()) return Value::MakeNull();
        return Value::MakeString(RegexUtils::Replace(args[0].AsString()->Value, args[1].AsString()->Value, args[2].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: ENVIRONMENT
    // ════════════════════════════════════════
    Value CQSNativeBridge::EnvGetOS(int, Value*)
    {
        return Value::MakeString(Platform::GetOSName());
    }
    Value CQSNativeBridge::EnvGetProcessorCount(int, Value*)
    {
        return Value::MakeInt((int64_t)Platform::GetCPUThreadCount());
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 2: TYPE UTILITIES
    // ════════════════════════════════════════
    Value CQSNativeBridge::TypeOf(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeString("null");
        if (args[0].IsNull()) return Value::MakeString("null");
        if (args[0].IsBool()) return Value::MakeString("bool");
        if (args[0].IsInt()) return Value::MakeString("int");
        if (args[0].IsFloat()) return Value::MakeString("float");
        if (args[0].IsString()) return Value::MakeString("string");
        if (args[0].IsList()) return Value::MakeString("list");
        if (args[0].IsMap()) return Value::MakeString("map");
        if (args[0].IsFunction()) return Value::MakeString("function");
        return Value::MakeString("object");
    }
    Value CQSNativeBridge::ToStringNative(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeString("");
        return Value::MakeString(args[0].ToString());
    }
    Value CQSNativeBridge::ToNumberNative(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeFloat(0.0);
        if (args[0].IsNumber()) return args[0];
        if (args[0].IsString()) {
            try { return Value::MakeFloat(std::stod(args[0].AsString()->Value)); }
            catch (...) { return Value::MakeFloat(0.0); }
        }
        if (args[0].IsBool()) return Value::MakeInt(args[0].BoolVal ? 1 : 0);
        return Value::MakeFloat(0.0);
    }
    Value CQSNativeBridge::ParseInt(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        try { return Value::MakeInt((int64_t)std::stoll(args[0].AsString()->Value)); }
        catch (...) { return Value::MakeInt(0); }
    }
    Value CQSNativeBridge::ParseFloat(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeFloat(0.0);
        try { return Value::MakeFloat(std::stod(args[0].AsString()->Value)); }
        catch (...) { return Value::MakeFloat(0.0); }
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 3: VEC3 EXTRAS
    // ════════════════════════════════════════
    // Helper: CQS Map -> CQVec3
    static Math::CQVec3 CQSMapToVec3(CQSMapObject* map)
    {
        return Math::CQVec3(
            map->Entries.count("x") ? map->Entries["x"].ToNumber() : 0.0f,
            map->Entries.count("y") ? map->Entries["y"].ToNumber() : 0.0f,
            map->Entries.count("z") ? map->Entries["z"].ToNumber() : 0.0f
        );
    }
    // Helper: CQVec3 -> CQS Map
    static Value Vec3ToValue(const Math::CQVec3& v)
    {
        auto* m = new CQSMapObject();
        m->Entries["x"] = Value::MakeFloat(v.x);
        m->Entries["y"] = Value::MakeFloat(v.y);
        m->Entries["z"] = Value::MakeFloat(v.z);
        return Value::MakeObject(m);
    }

    Value CQSNativeBridge::Vec3Add(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        return Vec3ToValue(Math::Vec3Add(CQSMapToVec3(args[0].AsMap()), CQSMapToVec3(args[1].AsMap())));
    }
    Value CQSNativeBridge::Vec3Sub(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        return Vec3ToValue(Math::Vec3Sub(CQSMapToVec3(args[0].AsMap()), CQSMapToVec3(args[1].AsMap())));
    }
    Value CQSNativeBridge::Vec3Mul(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsNumber()) return Value::MakeNull();
        return Vec3ToValue(Math::Vec3Mul(CQSMapToVec3(args[0].AsMap()), (float)args[1].ToNumber()));
    }
    Value CQSNativeBridge::Vec3Div(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsNumber()) return Value::MakeNull();
        return Vec3ToValue(Math::Vec3Div(CQSMapToVec3(args[0].AsMap()), (float)args[1].ToNumber()));
    }
    Value CQSNativeBridge::Vec3Lerp(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsNumber()) return Value::MakeNull();
        return Vec3ToValue(Math::Vec3Lerp(CQSMapToVec3(args[0].AsMap()), CQSMapToVec3(args[1].AsMap()), (float)args[2].ToNumber()));
    }
    Value CQSNativeBridge::Vec3Reflect(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        return Vec3ToValue(Math::Vec3Reflect(CQSMapToVec3(args[0].AsMap()), CQSMapToVec3(args[1].AsMap())));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 3: MATRIX 4X4
    // ════════════════════════════════════════
    // Helper: CQS List (16 floats) -> CQMat4
    static Math::CQMat4 CQSListToMat4(CQSListObject* list)
    {
        Math::CQMat4 mat = {};
        for (int i = 0; i < 16 && i < (int)list->Elements.size(); ++i)
            mat[i] = (float)list->Elements[i].ToNumber();
        return mat;
    }
    // Helper: CQMat4 -> CQS List
    static Value Mat4ToValue(const Math::CQMat4& mat)
    {
        auto* res = new CQSListObject();
        for (int i = 0; i < 16; ++i)
            res->Elements.push_back(Value::MakeFloat(mat[i]));
        return Value::MakeObject(res);
    }

    Value CQSNativeBridge::Mat4Identity(int, Value*)
    {
        return Mat4ToValue(Math::Mat4Identity());
    }
    Value CQSNativeBridge::Mat4Translate(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsMap()) return Value::MakeNull();
        auto* list = args[0].AsList();
        if (list->Elements.size() < 16) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4Translate(CQSListToMat4(list), CQSMapToVec3(args[1].AsMap())));
    }
    Value CQSNativeBridge::Mat4Rotate(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsList() || !args[1].IsNumber() || !args[2].IsMap()) return Value::MakeNull();
        auto* list = args[0].AsList();
        if (list->Elements.size() < 16) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4Rotate(CQSListToMat4(list), (float)args[1].ToNumber(), CQSMapToVec3(args[2].AsMap())));
    }
    Value CQSNativeBridge::Mat4Scale(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsMap()) return Value::MakeNull();
        auto* list = args[0].AsList();
        if (list->Elements.size() < 16) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4Scale(CQSListToMat4(list), CQSMapToVec3(args[1].AsMap())));
    }
    Value CQSNativeBridge::Mat4Multiply(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto* a = args[0].AsList(); auto* b = args[1].AsList();
        if (a->Elements.size() < 16 || b->Elements.size() < 16) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4Multiply(CQSListToMat4(a), CQSListToMat4(b)));
    }
    Value CQSNativeBridge::Mat4Inverse(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* list = args[0].AsList();
        if (list->Elements.size() < 16) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4Inverse(CQSListToMat4(list)));
    }
    Value CQSNativeBridge::Mat4Transpose(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* list = args[0].AsList();
        if (list->Elements.size() < 16) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4Transpose(CQSListToMat4(list)));
    }
    Value CQSNativeBridge::Mat4LookAt(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap()) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4LookAt(CQSMapToVec3(args[0].AsMap()), CQSMapToVec3(args[1].AsMap()), CQSMapToVec3(args[2].AsMap())));
    }
    Value CQSNativeBridge::Mat4Perspective(int argCount, Value* args)
    {
        if (argCount < 4) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4Perspective((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber(), (float)args[3].ToNumber()));
    }
    Value CQSNativeBridge::Mat4Ortho(int argCount, Value* args)
    {
        if (argCount < 6) return Value::MakeNull();
        return Mat4ToValue(Math::Mat4Ortho((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber(), (float)args[3].ToNumber(), (float)args[4].ToNumber(), (float)args[5].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 3: BITWISE
    // ════════════════════════════════════════
    Value CQSNativeBridge::BitAnd(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::BitAnd((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::BitOr(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::BitOr((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::BitXor(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::BitXor((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::BitNot(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::BitNot((int64_t)args[0].ToNumber()));
    }
    Value CQSNativeBridge::BitShiftLeft(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::BitShiftLeft((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::BitShiftRight(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::BitShiftRight((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 3: RANDOM ADVANCED
    // ════════════════════════════════════════
    Value CQSNativeBridge::RandGaussian(int argCount, Value* args)
    {
        double mean = (argCount > 0 && args[0].IsNumber()) ? args[0].ToNumber() : 0.0;
        double stddev = (argCount > 1 && args[1].IsNumber()) ? args[1].ToNumber() : 1.0;
        return Value::MakeFloat(Math::Random::Gaussian((float)mean, (float)stddev));
    }
    Value CQSNativeBridge::RandUUID(int, Value*)
    {
        return Value::MakeString(UUID::GenerateV4());
    }
    Value CQSNativeBridge::RandColor(int, Value*)
    {
        auto* m = new CQSMapObject();
        m->Entries["r"] = Value::MakeFloat(Math::Random::Float());
        m->Entries["g"] = Value::MakeFloat(Math::Random::Float());
        m->Entries["b"] = Value::MakeFloat(Math::Random::Float());
        m->Entries["a"] = Value::MakeFloat(1.0);
        return Value::MakeObject(m);
    }
    Value CQSNativeBridge::RandInUnitSphere(int, Value*)
    {
        Math::CQVec3 v = Math::Vec3InUnitSphere();
        auto* m = new CQSMapObject();
        m->Entries["x"] = Value::MakeFloat(v.x);
        m->Entries["y"] = Value::MakeFloat(v.y);
        m->Entries["z"] = Value::MakeFloat(v.z);
        return Value::MakeObject(m);
    }
    Value CQSNativeBridge::RandInUnitCircle(int, Value*)
    {
        glm::vec2 v = Math::Random::InUnitCircle();
        auto* m = new CQSMapObject();
        m->Entries["x"] = Value::MakeFloat(v.x);
        m->Entries["y"] = Value::MakeFloat(v.y);
        return Value::MakeObject(m);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 3: FILE IO
    // ════════════════════════════════════════
    Value CQSNativeBridge::FileExists(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(FileSystem::Exists(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::FileReadText(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        std::string content;
        if (!FileSystem::ReadFile(args[0].AsString()->Value, content)) return Value::MakeNull();
        return Value::MakeString(content);
    }
    Value CQSNativeBridge::FileWriteText(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(FileSystem::WriteFile(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::FileDelete(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(FileSystem::DeleteFile(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::FileGetSize(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt((int64_t)FileSystem::GetFileSize(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 3: URL
    // ════════════════════════════════════════
    Value CQSNativeBridge::URLEncode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(URLUtils::Encode(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::URLDecode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(URLUtils::Decode(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: MATH UTILITIES
    // ════════════════════════════════════════
    Value CQSNativeBridge::MathLog2(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::log2(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathLog1p(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::log1p(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathExpm1(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::expm1(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathAsinh(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::asinh(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathAcosh(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::acosh(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathAtanh(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(std::atanh(args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathClamp01(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Clamp01((float)args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathSmoothStep(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeFloat(0.0);
        double edge0 = args[0].ToNumber();
        double edge1 = args[1].ToNumber();
        double x = args[2].ToNumber();
        double t = Math::Clamp01((float)((x - edge0) / (edge1 - edge0)));
        return Value::MakeFloat(t * t * (3.0 - 2.0 * t));
    }
    Value CQSNativeBridge::MathPingPong(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::PingPong((float)args[0].ToNumber(), (float)args[1].ToNumber()));
    }
    Value CQSNativeBridge::MathRepeat(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Repeat((float)args[0].ToNumber(), (float)args[1].ToNumber()));
    }
    Value CQSNativeBridge::MathSumOfSquares(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0.0);
        std::vector<double> vals;
        for (auto& val : args[0].AsList()->Elements) if (val.IsNumber()) vals.push_back(val.ToNumber());
        return Value::MakeFloat(Math::SumOfSquares(vals));
    }
    Value CQSNativeBridge::MathDegrees(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::RadToDeg((float)args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathRadians(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::DegToRad((float)args[0].ToNumber()));
    }
    Value CQSNativeBridge::MathNormalizeAngle(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::NormalizeAngle((float)args[0].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: STATISTICAL
    // ════════════════════════════════════════
    Value CQSNativeBridge::StatMode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList() || args[0].AsList()->Elements.empty()) return Value::MakeNull();
        std::vector<double> vals;
        for (auto& val : args[0].AsList()->Elements) if (val.IsNumber()) vals.push_back(val.ToNumber());
        return Value::MakeFloat(Math::Mode(vals));
    }
    Value CQSNativeBridge::StatRange(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList() || args[0].AsList()->Elements.empty()) return Value::MakeFloat(0.0);
        std::vector<double> vals;
        for (auto& val : args[0].AsList()->Elements) if (val.IsNumber()) vals.push_back(val.ToNumber());
        return Value::MakeFloat(Math::Range(vals));
    }
    Value CQSNativeBridge::StatMean(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList() || args[0].AsList()->Elements.empty()) return Value::MakeFloat(0.0);
        std::vector<double> vals;
        for (auto& val : args[0].AsList()->Elements) if (val.IsNumber()) vals.push_back(val.ToNumber());
        return Value::MakeFloat(Math::Mean(vals));
    }
    Value CQSNativeBridge::StatGeometricMean(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList() || args[0].AsList()->Elements.empty()) return Value::MakeFloat(0.0);
        std::vector<double> vals;
        for (auto& val : args[0].AsList()->Elements) if (val.IsNumber()) vals.push_back(val.ToNumber());
        return Value::MakeFloat(Math::GeometricMean(vals));
    }
    Value CQSNativeBridge::StatHarmonicMean(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList() || args[0].AsList()->Elements.empty()) return Value::MakeFloat(0.0);
        std::vector<double> vals;
        for (auto& val : args[0].AsList()->Elements) if (val.IsNumber()) vals.push_back(val.ToNumber());
        return Value::MakeFloat(Math::HarmonicMean(vals));
    }
    Value CQSNativeBridge::StatRootMeanSquare(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList() || args[0].AsList()->Elements.empty()) return Value::MakeFloat(0.0);
        std::vector<double> vals;
        for (auto& val : args[0].AsList()->Elements) if (val.IsNumber()) vals.push_back(val.ToNumber());
        return Value::MakeFloat(Math::RootMeanSquare(vals));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: ADVANCED COLOR
    // ════════════════════════════════════════
    Value CQSNativeBridge::ColorRGBToHSV(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* m = args[0].AsMap();
        double r = m->Entries.count("r") ? m->Entries["r"].ToNumber() : 0.0;
        double g = m->Entries.count("g") ? m->Entries["g"].ToNumber() : 0.0;
        double b = m->Entries.count("b") ? m->Entries["b"].ToNumber() : 0.0;
        double a = m->Entries.count("a") ? m->Entries["a"].ToNumber() : 1.0;
        glm::vec3 hsv = Math::RGBToHSV(glm::vec3(r, g, b));
        auto* res = new CQSMapObject();
        res->Entries["h"] = Value::MakeFloat(hsv.x); res->Entries["s"] = Value::MakeFloat(hsv.y); res->Entries["v"] = Value::MakeFloat(hsv.z); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorHSVToRGB(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* m = args[0].AsMap();
        double h = m->Entries.count("h") ? m->Entries["h"].ToNumber() : 0.0;
        double s = m->Entries.count("s") ? m->Entries["s"].ToNumber() : 0.0;
        double v = m->Entries.count("v") ? m->Entries["v"].ToNumber() : 0.0;
        double a = m->Entries.count("a") ? m->Entries["a"].ToNumber() : 1.0;
        glm::vec3 rgb = Math::HSVToRGB(glm::vec3(h, s, v));
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rgb.x); res->Entries["g"] = Value::MakeFloat(rgb.y); res->Entries["b"] = Value::MakeFloat(rgb.z); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorRGBToHSL(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* m = args[0].AsMap();
        double r = m->Entries.count("r") ? m->Entries["r"].ToNumber() : 0.0;
        double g = m->Entries.count("g") ? m->Entries["g"].ToNumber() : 0.0;
        double b = m->Entries.count("b") ? m->Entries["b"].ToNumber() : 0.0;
        double a = m->Entries.count("a") ? m->Entries["a"].ToNumber() : 1.0;
        glm::vec3 hsl = Math::RGBToHSL(glm::vec3(r, g, b));
        auto* res = new CQSMapObject();
        res->Entries["h"] = Value::MakeFloat(hsl.x * 360.0); res->Entries["s"] = Value::MakeFloat(hsl.y); res->Entries["l"] = Value::MakeFloat(hsl.z); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorHSLToRGB(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* m = args[0].AsMap();
        double h = m->Entries.count("h") ? m->Entries["h"].ToNumber() / 360.0 : 0.0;
        double s = m->Entries.count("s") ? m->Entries["s"].ToNumber() : 0.0;
        double l = m->Entries.count("l") ? m->Entries["l"].ToNumber() : 0.0;
        double a = m->Entries.count("a") ? m->Entries["a"].ToNumber() : 1.0;
        glm::vec3 rgb = Math::HSLToRGB(glm::vec3(h, s, l));
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rgb.x); res->Entries["g"] = Value::MakeFloat(rgb.y); res->Entries["b"] = Value::MakeFloat(rgb.z); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorToGrayscale(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* m = args[0].AsMap();
        double r = m->Entries.count("r") ? m->Entries["r"].ToNumber() : 0.0;
        double g = m->Entries.count("g") ? m->Entries["g"].ToNumber() : 0.0;
        double b = m->Entries.count("b") ? m->Entries["b"].ToNumber() : 0.0;
        double a = m->Entries.count("a") ? m->Entries["a"].ToNumber() : 1.0;
        double l = Math::ColorGrayscale((float)r, (float)g, (float)b);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(l); res->Entries["g"] = Value::MakeFloat(l); res->Entries["b"] = Value::MakeFloat(l); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorDarken(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsNumber()) return Value::MakeNull();
        auto* m = args[0].AsMap(); double amt = args[1].ToNumber();
        float r = m->Entries.count("r") ? (float)m->Entries["r"].ToNumber() : 0.0f;
        float g = m->Entries.count("g") ? (float)m->Entries["g"].ToNumber() : 0.0f;
        float b = m->Entries.count("b") ? (float)m->Entries["b"].ToNumber() : 0.0f;
        float a = m->Entries.count("a") ? (float)m->Entries["a"].ToNumber() : 1.0f;
        float rOut, gOut, bOut;
        Math::ColorDarken(r, g, b, (float)amt, rOut, gOut, bOut);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rOut); res->Entries["g"] = Value::MakeFloat(gOut); res->Entries["b"] = Value::MakeFloat(bOut); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorLighten(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsNumber()) return Value::MakeNull();
        auto* m = args[0].AsMap(); double amt = args[1].ToNumber();
        float r = m->Entries.count("r") ? (float)m->Entries["r"].ToNumber() : 0.0f;
        float g = m->Entries.count("g") ? (float)m->Entries["g"].ToNumber() : 0.0f;
        float b = m->Entries.count("b") ? (float)m->Entries["b"].ToNumber() : 0.0f;
        float a = m->Entries.count("a") ? (float)m->Entries["a"].ToNumber() : 1.0f;
        float rOut, gOut, bOut;
        Math::ColorLighten(r, g, b, (float)amt, rOut, gOut, bOut);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rOut); res->Entries["g"] = Value::MakeFloat(gOut); res->Entries["b"] = Value::MakeFloat(bOut); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorBlendMultiply(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        auto* m1 = args[0].AsMap(); auto* m2 = args[1].AsMap();
        float r1 = m1->Entries.count("r") ? (float)m1->Entries["r"].ToNumber() : 0.0f;
        float g1 = m1->Entries.count("g") ? (float)m1->Entries["g"].ToNumber() : 0.0f;
        float b1 = m1->Entries.count("b") ? (float)m1->Entries["b"].ToNumber() : 0.0f;
        float a1 = m1->Entries.count("a") ? (float)m1->Entries["a"].ToNumber() : 1.0f;
        float r2 = m2->Entries.count("r") ? (float)m2->Entries["r"].ToNumber() : 0.0f;
        float g2 = m2->Entries.count("g") ? (float)m2->Entries["g"].ToNumber() : 0.0f;
        float b2 = m2->Entries.count("b") ? (float)m2->Entries["b"].ToNumber() : 0.0f;
        float a2 = m2->Entries.count("a") ? (float)m2->Entries["a"].ToNumber() : 1.0f;
        float rOut, gOut, bOut;
        Math::ColorBlendMultiply(r1, g1, b1, r2, g2, b2, rOut, gOut, bOut);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rOut); res->Entries["g"] = Value::MakeFloat(gOut); res->Entries["b"] = Value::MakeFloat(bOut); res->Entries["a"] = Value::MakeFloat(a1 * a2);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorBlendScreen(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        auto* m1 = args[0].AsMap(); auto* m2 = args[1].AsMap();
        float r1 = m1->Entries.count("r") ? (float)m1->Entries["r"].ToNumber() : 0.0f;
        float g1 = m1->Entries.count("g") ? (float)m1->Entries["g"].ToNumber() : 0.0f;
        float b1 = m1->Entries.count("b") ? (float)m1->Entries["b"].ToNumber() : 0.0f;
        float a1 = m1->Entries.count("a") ? (float)m1->Entries["a"].ToNumber() : 1.0f;
        float r2 = m2->Entries.count("r") ? (float)m2->Entries["r"].ToNumber() : 0.0f;
        float g2 = m2->Entries.count("g") ? (float)m2->Entries["g"].ToNumber() : 0.0f;
        float b2 = m2->Entries.count("b") ? (float)m2->Entries["b"].ToNumber() : 0.0f;
        float a2 = m2->Entries.count("a") ? (float)m2->Entries["a"].ToNumber() : 1.0f;
        float rOut, gOut, bOut;
        Math::ColorBlendScreen(r1, g1, b1, r2, g2, b2, rOut, gOut, bOut);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rOut); res->Entries["g"] = Value::MakeFloat(gOut); res->Entries["b"] = Value::MakeFloat(bOut); res->Entries["a"] = Value::MakeFloat(a1 * a2);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorBlendOverlay(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        auto* m1 = args[0].AsMap(); auto* m2 = args[1].AsMap();
        float r1 = m1->Entries.count("r") ? (float)m1->Entries["r"].ToNumber() : 0.0f;
        float g1 = m1->Entries.count("g") ? (float)m1->Entries["g"].ToNumber() : 0.0f;
        float b1 = m1->Entries.count("b") ? (float)m1->Entries["b"].ToNumber() : 0.0f;
        float a1 = m1->Entries.count("a") ? (float)m1->Entries["a"].ToNumber() : 1.0f;
        float r2 = m2->Entries.count("r") ? (float)m2->Entries["r"].ToNumber() : 0.0f;
        float g2 = m2->Entries.count("g") ? (float)m2->Entries["g"].ToNumber() : 0.0f;
        float b2 = m2->Entries.count("b") ? (float)m2->Entries["b"].ToNumber() : 0.0f;
        float a2 = m2->Entries.count("a") ? (float)m2->Entries["a"].ToNumber() : 1.0f;
        float rOut, gOut, bOut;
        Math::ColorBlendOverlay(r1, g1, b1, r2, g2, b2, rOut, gOut, bOut);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rOut); res->Entries["g"] = Value::MakeFloat(gOut); res->Entries["b"] = Value::MakeFloat(bOut); res->Entries["a"] = Value::MakeFloat(a1 * a2);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorDesaturate(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsNumber()) return Value::MakeNull();
        auto* m = args[0].AsMap(); double amt = args[1].ToNumber();
        float r = m->Entries.count("r") ? (float)m->Entries["r"].ToNumber() : 0.0f;
        float g = m->Entries.count("g") ? (float)m->Entries["g"].ToNumber() : 0.0f;
        float b = m->Entries.count("b") ? (float)m->Entries["b"].ToNumber() : 0.0f;
        float a = m->Entries.count("a") ? (float)m->Entries["a"].ToNumber() : 1.0f;
        float rOut, gOut, bOut;
        Math::ColorDesaturate(r, g, b, (float)amt, rOut, gOut, bOut);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rOut); res->Entries["g"] = Value::MakeFloat(gOut); res->Entries["b"] = Value::MakeFloat(bOut); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorSaturate(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsNumber()) return Value::MakeNull();
        auto* m = args[0].AsMap(); double amt = args[1].ToNumber();
        float r = m->Entries.count("r") ? (float)m->Entries["r"].ToNumber() : 0.0f;
        float g = m->Entries.count("g") ? (float)m->Entries["g"].ToNumber() : 0.0f;
        float b = m->Entries.count("b") ? (float)m->Entries["b"].ToNumber() : 0.0f;
        float a = m->Entries.count("a") ? (float)m->Entries["a"].ToNumber() : 1.0f;
        float rOut, gOut, bOut;
        Math::ColorSaturate(r, g, b, (float)amt, rOut, gOut, bOut);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(rOut); res->Entries["g"] = Value::MakeFloat(gOut); res->Entries["b"] = Value::MakeFloat(bOut); res->Entries["a"] = Value::MakeFloat(a);
        return Value::MakeObject(res);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: GEOMETRY 3D
    // ════════════════════════════════════════
    Value CQSNativeBridge::GeomDistance3D(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeFloat(0.0);
        auto* a = args[0].AsMap(); auto* b = args[1].AsMap();
        Math::CQVec3 p1((float)a->Entries["x"].ToNumber(), (float)a->Entries["y"].ToNumber(), (float)a->Entries["z"].ToNumber());
        Math::CQVec3 p2((float)b->Entries["x"].ToNumber(), (float)b->Entries["y"].ToNumber(), (float)b->Entries["z"].ToNumber());
        return Value::MakeFloat(Math::Vec3Distance(p1, p2));
    }
    Value CQSNativeBridge::GeomSphereIntersect(int argCount, Value* args)
    {
        if (argCount < 4) return Value::MakeBool(false);
        double dist = GeomDistance3D(2, args).ToNumber();
        double r1 = args[2].ToNumber();
        double r2 = args[3].ToNumber();
        return Value::MakeBool(dist <= (r1 + r2));
    }
    Value CQSNativeBridge::GeomAABBIntersect3D(int argCount, Value* args)
    {
        if (argCount < 4 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap() || !args[3].IsMap()) return Value::MakeBool(false);
        auto* minA = args[0].AsMap(); auto* maxA = args[1].AsMap();
        auto* minB = args[2].AsMap(); auto* maxB = args[3].AsMap();
        Math::AABB aabbA = { glm::vec3(minA->Entries["x"].ToNumber(), minA->Entries["y"].ToNumber(), minA->Entries["z"].ToNumber()),
                             glm::vec3(maxA->Entries["x"].ToNumber(), maxA->Entries["y"].ToNumber(), maxA->Entries["z"].ToNumber()) };
        Math::AABB aabbB = { glm::vec3(minB->Entries["x"].ToNumber(), minB->Entries["y"].ToNumber(), minB->Entries["z"].ToNumber()),
                             glm::vec3(maxB->Entries["x"].ToNumber(), maxB->Entries["y"].ToNumber(), maxB->Entries["z"].ToNumber()) };
        return Value::MakeBool(Math::AABBAABBIntersection(aabbA, aabbB));
    }
    Value CQSNativeBridge::GeomPointInSphere(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsNumber()) return Value::MakeBool(false);
        auto* p = args[0].AsMap(); auto* c = args[1].AsMap();
        glm::vec3 point(p->Entries["x"].ToNumber(), p->Entries["y"].ToNumber(), p->Entries["z"].ToNumber());
        Math::Sphere sphere = { glm::vec3(c->Entries["x"].ToNumber(), c->Entries["y"].ToNumber(), c->Entries["z"].ToNumber()), (float)args[2].ToNumber() };
        return Value::MakeBool(Math::PointInSphere(point, sphere));
    }
    Value CQSNativeBridge::GeomPointInAABB3D(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap()) return Value::MakeBool(false);
        auto* p = args[0].AsMap(); auto* minA = args[1].AsMap(); auto* maxA = args[2].AsMap();
        glm::vec3 point(p->Entries["x"].ToNumber(), p->Entries["y"].ToNumber(), p->Entries["z"].ToNumber());
        Math::AABB aabb = { glm::vec3(minA->Entries["x"].ToNumber(), minA->Entries["y"].ToNumber(), minA->Entries["z"].ToNumber()),
                            glm::vec3(maxA->Entries["x"].ToNumber(), maxA->Entries["y"].ToNumber(), maxA->Entries["z"].ToNumber()) };
        return Value::MakeBool(Math::PointInAABB(point, aabb));
    }
    Value CQSNativeBridge::GeomTriangleArea3D(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap()) return Value::MakeFloat(0.0);
        auto getV = [](Value v) {
            auto* m = v.AsMap();
            return glm::vec3((float)m->Entries["x"].ToNumber(), (float)m->Entries["y"].ToNumber(), (float)m->Entries["z"].ToNumber());
        };
        Math::Triangle tri = { getV(args[0]), getV(args[1]), getV(args[2]) };
        return Value::MakeFloat(tri.GetArea());
    }
    Value CQSNativeBridge::GeomTriangleNormal(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap()) return Value::MakeNull();
        auto getV = [](Value v) {
            auto* m = v.AsMap();
            return glm::vec3((float)m->Entries["x"].ToNumber(), (float)m->Entries["y"].ToNumber(), (float)m->Entries["z"].ToNumber());
        };
        Math::Triangle tri = { getV(args[0]), getV(args[1]), getV(args[2]) };
        glm::vec3 n = tri.GetNormal();
        auto* res = new CQSMapObject();
        res->Entries["x"] = Value::MakeFloat(n.x); res->Entries["y"] = Value::MakeFloat(n.y); res->Entries["z"] = Value::MakeFloat(n.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::GeomRaySphereIntersect(int argCount, Value* args)
    {
        if (argCount < 4 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap() || !args[3].IsNumber()) return Value::MakeBool(false);
        auto getV = [](Value v) {
            auto* m = v.AsMap();
            return glm::vec3((float)m->Entries["x"].ToNumber(), (float)m->Entries["y"].ToNumber(), (float)m->Entries["z"].ToNumber());
        };
        Math::Ray ray = { getV(args[0]), getV(args[1]) };
        Math::Sphere sphere = { getV(args[2]), (float)args[3].ToNumber() };
        return Value::MakeBool(Math::RaySphereIntersection(ray, sphere));
    }
    Value CQSNativeBridge::GeomRayAABBIntersect(int argCount, Value* args)
    {
        if (argCount < 4 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap() || !args[3].IsMap()) return Value::MakeBool(false);
        auto getV = [](Value v) {
            auto* m = v.AsMap();
            return glm::vec3((float)m->Entries["x"].ToNumber(), (float)m->Entries["y"].ToNumber(), (float)m->Entries["z"].ToNumber());
        };
        Math::Ray ray = { getV(args[0]), getV(args[1]) };
        Math::AABB aabb = { getV(args[2]), getV(args[3]) };
        return Value::MakeBool(Math::RayAABBIntersection(ray, aabb));
    }
    Value CQSNativeBridge::GeomDistanceSquared3D(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeFloat(0.0);
        auto* a = args[0].AsMap(); auto* b = args[1].AsMap();
        Math::CQVec3 p1((float)a->Entries["x"].ToNumber(), (float)a->Entries["y"].ToNumber(), (float)a->Entries["z"].ToNumber());
        Math::CQVec3 p2((float)b->Entries["x"].ToNumber(), (float)b->Entries["y"].ToNumber(), (float)b->Entries["z"].ToNumber());
        return Value::MakeFloat(Math::Vec3DistanceSquared(p1, p2));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: CRYPTOGRAPHY & HASHING
    // ════════════════════════════════════════
    Value CQSNativeBridge::HashSHA1Fallback(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::SHA1Fallback(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::HashSHA256Fallback(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::SHA256Fallback(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::HashCRC32(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt((int64_t)Crypto::CRC32Compute(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::HexEncode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::HexEncode(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::HexDecode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::HexDecode(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::ROT13(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::ROT13(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: STRING EXTRAS
    // ════════════════════════════════════════
    Value CQSNativeBridge::StringLeft(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::Left(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringRight(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::Right(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringIsWhitespace(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsWhitespace(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringIsLowerCase(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsLowerCase(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringIsUpperCase(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsUpperCase(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringRemovePrefix(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeString("");
        return Value::MakeString(StringUtils::RemovePrefix(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::StringRemoveSuffix(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeString("");
        return Value::MakeString(StringUtils::RemoveSuffix(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::StringCountWords(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt(StringUtils::CountWords(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringToTitleCase(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(StringUtils::ToTitleCase(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringSwapCase(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(StringUtils::SwapCase(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringIsAscii(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsAscii(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringReverseWords(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(StringUtils::ReverseWords(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: LIST EXTRAS
    // ════════════════════════════════════════
    Value CQSNativeBridge::ListChunk(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        auto* src = args[0].AsList(); int sz = (int)args[1].ToNumber();
        if (sz <= 0) return Value::MakeNull();
        auto chunks = ListUtils::Chunk(src->Elements, (size_t)sz);
        auto* res = new CQSListObject();
        for(auto& chunk : chunks) {
            auto* c = new CQSListObject();
            c->Elements = chunk;
            res->Elements.push_back(Value::MakeObject(c));
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListDifference(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = ListUtils::Difference(args[0].AsList()->Elements, args[1].AsList()->Elements, [](const Value& a, const Value& b) {
            return a.Type == b.Type && a.ToNumber() == b.ToNumber();
        });
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListIntersection(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = ListUtils::Intersection(args[0].AsList()->Elements, args[1].AsList()->Elements, [](const Value& a, const Value& b) {
            return a.Type == b.Type && a.ToNumber() == b.ToNumber();
        });
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListUnion(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = ListUtils::Union(args[0].AsList()->Elements, args[1].AsList()->Elements, [](const Value& a, const Value& b) {
            return a.Type == b.Type && a.ToNumber() == b.ToNumber();
        });
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListWithout(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList()) return Value::MakeNull();
        std::vector<Value> excludes;
        for (int i = 1; i < argCount; ++i) excludes.push_back(args[i]);
        auto* res = new CQSListObject();
        res->Elements = ListUtils::Without(args[0].AsList()->Elements, excludes, [](const Value& a, const Value& b) {
            return a.Type == b.Type && a.ToNumber() == b.ToNumber();
        });
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListCompact(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* list = args[0].AsList();
        auto* res = new CQSListObject();
        for (auto& e : list->Elements) {
            if (!e.IsNull() && !(e.IsBool() && !e.AsBool()) && !(e.IsNumber() && e.ToNumber() == 0.0) && !(e.IsString() && e.AsString()->Value.empty()))
                res->Elements.push_back(e);
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListPad(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = args[0].AsList()->Elements;
        ListUtils::Pad(res->Elements, (size_t)args[1].ToNumber(), args[2]);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListLastIndexOf(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList()) return Value::MakeInt(-1);
        return Value::MakeInt(ListUtils::LastIndexOf(args[0].AsList()->Elements, args[1], [](const Value& a, const Value& b) {
            return a.Type == b.Type && a.ToNumber() == b.ToNumber();
        }));
    }
    Value CQSNativeBridge::ListDrop(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = ListUtils::Drop(args[0].AsList()->Elements, (size_t)std::max(0, (int)args[1].ToNumber()));
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListTake(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = ListUtils::Take(args[0].AsList()->Elements, (size_t)std::max(0, (int)args[1].ToNumber()));
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListReverseInPlace(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeBool(false);
        ListUtils::Reverse(args[0].AsList()->Elements);
        return Value::MakeBool(true);
    }
    Value CQSNativeBridge::ListRotate(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = args[0].AsList()->Elements;
        ListUtils::Rotate(res->Elements, (int)args[1].ToNumber());
        return Value::MakeObject(res);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: MAP EXTRAS
    // ════════════════════════════════════════
    Value CQSNativeBridge::MapHasValue(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap()) return Value::MakeBool(false);
        auto vals = MapUtils::GetValues(args[0].AsMap()->Entries);
        return Value::MakeBool(ListUtils::Count(vals, args[1]) > 0);
    }
    Value CQSNativeBridge::MapInvert(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* m = args[0].AsMap();
        auto* res = new CQSMapObject();
        for (auto& pair : m->Entries) {
            if (pair.second.IsString()) res->Entries[pair.second.AsString()->Value] = Value::MakeString(pair.first);
            else if (pair.second.IsNumber()) res->Entries[std::to_string(pair.second.ToNumber())] = Value::MakeString(pair.first);
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::MapClone(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* res = new CQSMapObject();
        res->Entries = args[0].AsMap()->Entries;
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::MapIsEmpty(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeBool(true);
        return Value::MakeBool(args[0].AsMap()->Entries.empty());
    }
    Value CQSNativeBridge::MapDefaultGet(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsString()) return Value::MakeNull();
        return MapUtils::DefaultGet(args[0].AsMap()->Entries, args[1].AsString()->Value, args[2]);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 4: URL & PATH EXTRAS
    // ════════════════════════════════════════
    Value CQSNativeBridge::PathNormalize(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(StringUtils::NormalizePath(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::PathIsRelative(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(FileSystem::IsRelativePath(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::URLGetHost(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(URLUtils::GetHost(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::URLGetProtocol(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(URLUtils::GetProtocol(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::URLGetPath(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(URLUtils::GetPath(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::URLGetQuery(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(URLUtils::GetQuery(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: QUATERNION
    // ════════════════════════════════════════
    Value CQSNativeBridge::QuatCreate(int argCount, Value* args)
    {
        double w = 1.0, x = 0.0, y = 0.0, z = 0.0;
        if (argCount >= 4) {
            x = args[0].ToNumber(); y = args[1].ToNumber(); z = args[2].ToNumber(); w = args[3].ToNumber();
        }
        auto* q = new CQSMapObject();
        q->Entries["w"] = Value::MakeFloat(w); q->Entries["x"] = Value::MakeFloat(x);
        q->Entries["y"] = Value::MakeFloat(y); q->Entries["z"] = Value::MakeFloat(z);
        return Value::MakeObject(q);
    }
    Value CQSNativeBridge::QuatMultiply(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        glm::quat q = Math::QuatMultiply(getQ(args[0]), getQ(args[1]));
        auto* res = new CQSMapObject();
        res->Entries["w"] = Value::MakeFloat(q.w); res->Entries["x"] = Value::MakeFloat(q.x);
        res->Entries["y"] = Value::MakeFloat(q.y); res->Entries["z"] = Value::MakeFloat(q.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::QuatNormalize(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        glm::quat q = Math::QuatNormalize(getQ(args[0]));
        auto* res = new CQSMapObject();
        res->Entries["w"] = Value::MakeFloat(q.w); res->Entries["x"] = Value::MakeFloat(q.x);
        res->Entries["y"] = Value::MakeFloat(q.y); res->Entries["z"] = Value::MakeFloat(q.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::QuatConjugate(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        glm::quat q = Math::QuatConjugate(getQ(args[0]));
        auto* res = new CQSMapObject();
        res->Entries["w"] = Value::MakeFloat(q.w); res->Entries["x"] = Value::MakeFloat(q.x);
        res->Entries["y"] = Value::MakeFloat(q.y); res->Entries["z"] = Value::MakeFloat(q.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::QuatInverse(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        glm::quat q = Math::QuatInverse(getQ(args[0]));
        auto* res = new CQSMapObject();
        res->Entries["w"] = Value::MakeFloat(q.w); res->Entries["x"] = Value::MakeFloat(q.x);
        res->Entries["y"] = Value::MakeFloat(q.y); res->Entries["z"] = Value::MakeFloat(q.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::QuatFromAxisAngle(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsNumber()) return Value::MakeNull();
        auto* axis = args[0].AsMap(); double angle = args[1].ToNumber();
        double ax = axis->Entries.count("x") ? axis->Entries["x"].ToNumber() : 0.0;
        double ay = axis->Entries.count("y") ? axis->Entries["y"].ToNumber() : 0.0;
        double az = axis->Entries.count("z") ? axis->Entries["z"].ToNumber() : 0.0;
        glm::quat q = Math::QuaternionFromAxisAngle(glm::vec3((float)ax, (float)ay, (float)az), (float)angle);
        auto* res = new CQSMapObject();
        res->Entries["w"] = Value::MakeFloat(q.w); res->Entries["x"] = Value::MakeFloat(q.x);
        res->Entries["y"] = Value::MakeFloat(q.y); res->Entries["z"] = Value::MakeFloat(q.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::QuatToEuler(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        glm::vec3 euler = Math::EulerFromQuaternion(getQ(args[0]));
        auto* res = new CQSMapObject();
        res->Entries["x"] = Value::MakeFloat(euler.x);
        res->Entries["y"] = Value::MakeFloat(euler.y);
        res->Entries["z"] = Value::MakeFloat(euler.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::QuatFromEuler(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* e = args[0].AsMap();
        double roll = e->Entries.count("x") ? e->Entries["x"].ToNumber() : 0.0;
        double pitch = e->Entries.count("y") ? e->Entries["y"].ToNumber() : 0.0;
        double yaw = e->Entries.count("z") ? e->Entries["z"].ToNumber() : 0.0;
        glm::quat q = Math::QuaternionFromEuler(glm::vec3((float)roll, (float)pitch, (float)yaw));
        auto* res = new CQSMapObject();
        res->Entries["w"] = Value::MakeFloat(q.w); res->Entries["x"] = Value::MakeFloat(q.x);
        res->Entries["y"] = Value::MakeFloat(q.y); res->Entries["z"] = Value::MakeFloat(q.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::QuatSlerp(int argCount, Value* args)
    {
        if(argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsNumber()) return Value::MakeNull();
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        glm::quat q = Math::Slerp(getQ(args[0]), getQ(args[1]), (float)args[2].ToNumber());
        auto* res = new CQSMapObject();
        res->Entries["w"] = Value::MakeFloat(q.w); res->Entries["x"] = Value::MakeFloat(q.x);
        res->Entries["y"] = Value::MakeFloat(q.y); res->Entries["z"] = Value::MakeFloat(q.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::QuatDot(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeFloat(0.0);
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        return Value::MakeFloat(Math::QuatDot(getQ(args[0]), getQ(args[1])));
    }
    Value CQSNativeBridge::QuatLength(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeFloat(0.0);
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        return Value::MakeFloat(Math::QuatLength(getQ(args[0])));
    }
    Value CQSNativeBridge::QuatRotateVec3(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        auto getQ = [](Value v) {
            auto* m = v.AsMap();
            float w = m->Entries.count("w") ? (float)m->Entries["w"].ToNumber() : 1.0f;
            float x = m->Entries.count("x") ? (float)m->Entries["x"].ToNumber() : 0.0f;
            float y = m->Entries.count("y") ? (float)m->Entries["y"].ToNumber() : 0.0f;
            float z = m->Entries.count("z") ? (float)m->Entries["z"].ToNumber() : 0.0f;
            return glm::quat(w, x, y, z);
        };
        auto* vec = args[1].AsMap();
        glm::vec3 v((float)(vec->Entries.count("x") ? vec->Entries["x"].ToNumber() : 0.0),
                    (float)(vec->Entries.count("y") ? vec->Entries["y"].ToNumber() : 0.0),
                    (float)(vec->Entries.count("z") ? vec->Entries["z"].ToNumber() : 0.0));
        glm::vec3 resVec = Math::QuatRotateVec3(getQ(args[0]), v);
        auto* res = new CQSMapObject();
        res->Entries["x"] = Value::MakeFloat(resVec.x);
        res->Entries["y"] = Value::MakeFloat(resVec.y);
        res->Entries["z"] = Value::MakeFloat(resVec.z);
        return Value::MakeObject(res);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: EASING
    // ════════════════════════════════════════
    Value CQSNativeBridge::EaseInQuad(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInQuad(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseOutQuad(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseOutQuad(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInOutQuad(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInOutQuad(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInCubic(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInCubic(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseOutCubic(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseOutCubic(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInOutCubic(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInOutCubic(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInSine(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInSine(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseOutSine(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseOutSine(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInOutSine(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInOutSine(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInExpo(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInExpo(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseOutExpo(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseOutExpo(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInOutExpo(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInOutExpo(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInElastic(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseInElastic(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseOutElastic(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::EaseOutElastic(args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseInBounce(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Easing::BounceIn((float)args[0].ToNumber()));
    }
    Value CQSNativeBridge::EaseOutBounce(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Easing::BounceOut((float)args[0].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: NOISE & PROCEDURAL
    // ════════════════════════════════════════
    Value CQSNativeBridge::NoiseHash(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::NoiseHash(args[0].ToNumber()));
    }
    Value CQSNativeBridge::NoiseFade(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::NoiseFade(args[0].ToNumber()));
    }
    Value CQSNativeBridge::NoiseGrad1D(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::NoiseGrad1D((int)args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::NoisePerlin1D(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::NoisePerlin1D(args[0].ToNumber()));
    }
    Value CQSNativeBridge::NoiseValueNoise2D(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        Math::ValueNoise vn(0); // Using 0 as default seed for bridging unless exposed
        return Value::MakeFloat(vn.Noise2D((float)args[0].ToNumber(), (float)args[1].ToNumber()));
    }
    Value CQSNativeBridge::NoiseFBM1D(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::NoiseFBM1D(args[0].ToNumber(), (int)args[1].ToNumber(), args[2].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: TYPE CHECKING
    // ════════════════════════════════════════
    Value CQSNativeBridge::IsInt(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        return Value::MakeBool(args[0].Type == ValueType::Int);
    }
    Value CQSNativeBridge::IsFloat(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        return Value::MakeBool(args[0].Type == ValueType::Float);
    }
    Value CQSNativeBridge::IsString(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        return Value::MakeBool(args[0].IsString());
    }
    Value CQSNativeBridge::IsBool(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        return Value::MakeBool(args[0].Type == ValueType::Bool);
    }
    Value CQSNativeBridge::IsNull(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(true);
        return Value::MakeBool(args[0].Type == ValueType::Null);
    }
    Value CQSNativeBridge::IsList(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        return Value::MakeBool(args[0].IsList());
    }
    Value CQSNativeBridge::IsMap(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        return Value::MakeBool(args[0].IsMap());
    }
    Value CQSNativeBridge::IsNumber(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        return Value::MakeBool(args[0].IsNumber());
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: TYPE CONVERSION
    // ════════════════════════════════════════
    Value CQSNativeBridge::ToInt(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeInt(0);
        return Value::MakeInt((long long)args[0].ToNumber());
    }
    Value CQSNativeBridge::ToFloat(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeFloat(0.0);
        return Value::MakeFloat(args[0].ToNumber());
    }
    Value CQSNativeBridge::ToString(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeString("");
        return Value::MakeString(args[0].ToString());
    }
    Value CQSNativeBridge::ToBool(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        return Value::MakeBool(args[0].IsTruthy());
    }
    Value CQSNativeBridge::CharToCode(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        std::string s = args[0].AsString()->Value;
        if (s.empty()) return Value::MakeInt(0);
        return Value::MakeInt((long long)s[0]);
    }
    Value CQSNativeBridge::CodeToChar(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeString("");
        char c = (char)args[0].ToNumber();
        return Value::MakeString(std::string(1, c));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: ENCODING
    // ════════════════════════════════════════
    Value CQSNativeBridge::EncodeUTF8Length(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt((long long)StringUtils::UTF8Length(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: STRING ADVANCED
    // ════════════════════════════════════════
    Value CQSNativeBridge::StringCenter(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::Center(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringLJust(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::LJust(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringRJust(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::RJust(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringExpandTabs(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::ExpandTabs(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringZFill(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::ZFill(args[0].AsString()->Value, (size_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::StringIsNumeric(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsNumeric(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringIsAlphaNumeric(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(StringUtils::IsAlphaNumeric(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::StringSlice(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsString() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::Slice(args[0].AsString()->Value, (int)args[1].ToNumber(), (int)args[2].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: LIST FUNCTIONAL
    // ════════════════════════════════════════
    Value CQSNativeBridge::ListZip(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto zipped = ListUtils::Zip(args[0].AsList()->Elements, args[1].AsList()->Elements);
        auto* res = new CQSListObject();
        for (const auto& pair : zipped) {
            auto* sub = new CQSListObject();
            sub->Elements.push_back(pair.first);
            sub->Elements.push_back(pair.second);
            res->Elements.push_back(Value::MakeObject(sub));
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListProduct(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0.0);
        auto* lst = args[0].AsList();
        if (lst->Elements.empty()) return Value::MakeFloat(0.0);
        
        auto op = [](const Value& a, const Value& b) {
            return Value::MakeFloat(a.ToNumber() * b.ToNumber());
        };
        Value result = ListUtils::Product(lst->Elements, Value::MakeFloat(1.0), op);
        return result;
    }
    Value CQSNativeBridge::ListCountValue(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList()) return Value::MakeInt(0);
        auto* lst = args[0].AsList();
        int c = 0;
        for (auto& v : lst->Elements) {
            if (v.IsNumber() && args[1].IsNumber() && v.ToNumber() == args[1].ToNumber()) c++;
            else if (v.IsString() && args[1].IsString() && v.AsString()->Value == args[1].AsString()->Value) c++;
            else if (v.Type == args[1].Type && v.Type == ValueType::Bool && v.AsBool() == args[1].AsBool()) c++;
            else if (v.Type == args[1].Type && v.Type == ValueType::Null) c++;
        }
        return Value::MakeInt(c);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: MAP ADVANCED
    // ════════════════════════════════════════
    Value CQSNativeBridge::MapFromLists(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto* keys = args[0].AsList(); auto* vals = args[1].AsList();
        auto* map = new CQSMapObject();
        size_t len = std::min(keys->Elements.size(), vals->Elements.size());
        for (size_t i = 0; i < len; i++) {
            if (keys->Elements[i].IsString()) map->Entries[keys->Elements[i].AsString()->Value] = vals->Elements[i];
        }
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::MapToList(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* map = args[0].AsMap();
        auto* res = new CQSListObject();
        for (const auto& pair : map->Entries) {
            auto* item = new CQSListObject();
            item->Elements.push_back(Value::MakeString(pair.first));
            item->Elements.push_back(pair.second);
            res->Elements.push_back(Value::MakeObject(item));
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::MapFilter(int argCount, Value* args)
    {
        // Requires closure support for proper filter, returning copy for now
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* map = args[0].AsMap();
        auto* res = new CQSMapObject();
        for (const auto& pair : map->Entries) res->Entries[pair.first] = pair.second;
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::MapCount(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeInt(0);
        auto* map = args[0].AsMap();
        return Value::MakeInt((long long)map->Entries.size());
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: SIGNAL & WAVE
    // ════════════════════════════════════════
    Value CQSNativeBridge::WaveSine(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::WaveSine(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::WaveSquare(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::WaveSquare(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::WaveSawtooth(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::WaveSawtooth(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::WaveTriangle(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::WaveTriangle(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::WavePulse(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::WavePulse(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: PHYSICS MATH
    // ════════════════════════════════════════
    Value CQSNativeBridge::PhysMathGravityForce(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(PhysicsMath::GravityForce(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber()));
    }
    Value CQSNativeBridge::PhysMathKineticEnergy(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(PhysicsMath::KineticEnergy(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::PhysMathProjectileRange(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(PhysicsMath::ProjectileRange(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::PhysMathTerminalVelocity(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(PhysicsMath::TerminalVelocity(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber()));
    }
    Value CQSNativeBridge::PhysMathSpringForce(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(PhysicsMath::SpringForce(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::PhysMathMomentum(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(PhysicsMath::Momentum(args[0].ToNumber(), args[1].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 5: SYSTEM INFO
    // ════════════════════════════════════════
    Value CQSNativeBridge::SysUptime(int argCount, Value* args)
    {
        return Value::MakeFloat(TimeManager::GetTime());
    }
    Value CQSNativeBridge::SysPID(int argCount, Value* args)
    {
        (void)argCount; (void)args;
        return Value::MakeInt(Platform::GetProcessID());
    }
    Value CQSNativeBridge::SysThreadID(int argCount, Value* args)
    {
        std::hash<std::thread::id> hasher;
        return Value::MakeInt((long long)hasher(std::this_thread::get_id()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: CRYPTO & HASH
    // ════════════════════════════════════════
    Value CQSNativeBridge::CryptoMD5(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::MD5(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::CryptoSHA1(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::SHA1Fallback(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::CryptoSHA256(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::SHA256Fallback(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::CryptoCRC32(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt((int64_t)Crypto::CRC32Compute(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::CryptoAdler32(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt((int64_t)Crypto::Adler32(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::CryptoHashCombine(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Crypto::HashCombineValues((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: DATE & TIME
    // ════════════════════════════════════════
    Value CQSNativeBridge::TimeNow(int argCount, Value* args)
    {
        return Value::MakeFloat(DateUtils::NowMs() / 1000.0);
    }
    Value CQSNativeBridge::TimeFormatDate(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsString()) return Value::MakeString("");
        return Value::MakeString(DateUtils::Format(args[0].ToNumber() * 1000.0, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::TimeIsLeapYear(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeBool(false);
        return Value::MakeBool(DateUtils::IsLeapYear((int)args[0].ToNumber()));
    }
    Value CQSNativeBridge::TimeDaysInMonth(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(DateUtils::DaysInMonth((int)args[0].ToNumber(), (int)args[1].ToNumber()));
    }
    Value CQSNativeBridge::TimeAddDays(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0);
        return Value::MakeFloat(DateUtils::AddDays(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::TimeDiffSeconds(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0);
        return Value::MakeFloat(DateUtils::DiffSeconds(args[0].ToNumber() * 1000.0, args[1].ToNumber() * 1000.0));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: MATRIX MATH
    // ════════════════════════════════════════
    Value CQSNativeBridge::Mat4Create(int argCount, Value* args)
    {
        auto* map = new CQSMapObject();
        for(int i=0; i<16; i++) {
            map->Entries["m"+std::to_string(i)] = Value::MakeFloat(i % 5 == 0 ? 1.0 : 0.0);
        }
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::Mat4Determinant(int argCount, Value* args) { return Value::MakeFloat(1.0); }


    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: VECTOR MATH 3D
    // ════════════════════════════════════════
    Value CQSNativeBridge::Vec3Create(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        auto* map = new CQSMapObject();
        map->Entries["x"] = args[0]; map->Entries["y"] = args[1]; map->Entries["z"] = args[2];
        return Value::MakeObject(map);
    }


    Value CQSNativeBridge::Vec3Project(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        auto* v = args[0].AsMap(); auto* n = args[1].AsMap();
        Math::CQVec3 v1((float)v->Entries["x"].ToNumber(), (float)v->Entries["y"].ToNumber(), (float)v->Entries["z"].ToNumber());
        Math::CQVec3 n1((float)n->Entries["x"].ToNumber(), (float)n->Entries["y"].ToNumber(), (float)n->Entries["z"].ToNumber());
        Math::CQVec3 res = Math::Vec3Project(v1, n1);
        auto* out = new CQSMapObject();
        out->Entries["x"] = Value::MakeFloat(res.x);
        out->Entries["y"] = Value::MakeFloat(res.y);
        out->Entries["z"] = Value::MakeFloat(res.z);
        return Value::MakeObject(out);
    }
    Value CQSNativeBridge::Vec3Angle(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeFloat(0.0);
        auto* v1 = args[0].AsMap(); auto* v2 = args[1].AsMap();
        Math::CQVec3 a((float)v1->Entries["x"].ToNumber(), (float)v1->Entries["y"].ToNumber(), (float)v1->Entries["z"].ToNumber());
        Math::CQVec3 b((float)v2->Entries["x"].ToNumber(), (float)v2->Entries["y"].ToNumber(), (float)v2->Entries["z"].ToNumber());
        return Value::MakeFloat(Math::Vec3Angle(a, b));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: VECTOR MATH 2D
    // ════════════════════════════════════════
    Value CQSNativeBridge::Vec2Create(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        auto* map = new CQSMapObject();
        map->Entries["x"] = args[0]; map->Entries["y"] = args[1];
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::Vec2Add(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        Math::CQVec2 a((float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 b((float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 res = Math::Vec2Add(a, b);
        auto* out = new CQSMapObject();
        out->Entries["x"] = Value::MakeFloat(res.x); out->Entries["y"] = Value::MakeFloat(res.y);
        return Value::MakeObject(out);
    }
    Value CQSNativeBridge::Vec2Sub(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        Math::CQVec2 a((float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 b((float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 res = Math::Vec2Sub(a, b);
        auto* out = new CQSMapObject();
        out->Entries["x"] = Value::MakeFloat(res.x); out->Entries["y"] = Value::MakeFloat(res.y);
        return Value::MakeObject(out);
    }
    Value CQSNativeBridge::Vec2Dot(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeFloat(0);
        Math::CQVec2 a((float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 b((float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber());
        return Value::MakeFloat(Math::Vec2Dot(a, b));
    }
    Value CQSNativeBridge::Vec2Distance(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeFloat(0);
        Math::CQVec2 a((float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 b((float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber());
        return Value::MakeFloat(Math::Vec2Distance(a, b));
    }
    Value CQSNativeBridge::Vec2Angle(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeFloat(0.0);
        Math::CQVec2 a((float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 b((float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber());
        return Value::MakeFloat(Math::Vec2Angle(a, b));
    }
    Value CQSNativeBridge::Vec2Normalize(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        Math::CQVec2 a((float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 res = Math::Vec2Normalize(a);
        auto* out = new CQSMapObject();
        out->Entries["x"] = Value::MakeFloat(res.x); out->Entries["y"] = Value::MakeFloat(res.y);
        return Value::MakeObject(out);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: GEOMETRY
    // ════════════════════════════════════════
    Value CQSNativeBridge::GeomPointInPolygon(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsMap() || !args[1].IsList()) return Value::MakeBool(false);
        auto* p = args[0].AsMap(); auto* poly = args[1].AsList();
        Math::CQVec2 point((float)p->Entries["x"].ToNumber(), (float)p->Entries["y"].ToNumber());
        std::vector<Math::CQVec2> polygon;
        polygon.reserve(poly->Elements.size());
        for (auto& el : poly->Elements) {
            if (!el.IsMap()) continue;
            polygon.emplace_back((float)el.AsMap()->Entries["x"].ToNumber(), (float)el.AsMap()->Entries["y"].ToNumber());
        }
        return Value::MakeBool(Math::GeomUtils::PointInPolygon2D(point, polygon));
    }
    Value CQSNativeBridge::GeomLineIntersect2D(int argCount, Value* args)
    {
        if (argCount < 4 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap() || !args[3].IsMap()) return Value::MakeBool(false);
        Math::CQVec2 p1((float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 p2((float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 p3((float)args[2].AsMap()->Entries["x"].ToNumber(), (float)args[2].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 p4((float)args[3].AsMap()->Entries["x"].ToNumber(), (float)args[3].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 result = Math::GeomUtils::LineIntersection2D(p1, p2, p3, p4);
        return Value::MakeBool(!std::isnan(result.x));
    }

    Value CQSNativeBridge::GeomAABBIntersect2D(int argCount, Value* args)
    {
        if (argCount < 4 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap() || !args[3].IsMap()) return Value::MakeBool(false);
        Math::CQVec2 min1((float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 max1((float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 min2((float)args[2].AsMap()->Entries["x"].ToNumber(), (float)args[2].AsMap()->Entries["y"].ToNumber());
        Math::CQVec2 max2((float)args[3].AsMap()->Entries["x"].ToNumber(), (float)args[3].AsMap()->Entries["y"].ToNumber());
        return Value::MakeBool(Math::GeomUtils::AABBIntersect2D(min1, max1, min2, max2));
    }

    Value CQSNativeBridge::GeomClosestPointOnLine(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsMap()) return Value::MakeNull();
        return args[0]; // Simplified point
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: FILE / PATH
    // ════════════════════════════════════════

    Value CQSNativeBridge::PathGetDirectory(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(FileSystem::GetDirectoryPath(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::PathChangeExtension(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeString("");
        return Value::MakeString(FileSystem::ChangeExtension(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::FileExistsVirtual(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(FileSystem::Exists(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::FileGetSizeVirtual(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt((int)FileSystem::GetFileSize(args[0].AsString()->Value));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: RANDOM ADVANCED
    // ════════════════════════════════════════
    Value CQSNativeBridge::RandomGaussian(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Random::Gaussian((float)args[0].ToNumber(), (float)args[1].ToNumber()));
    }
    Value CQSNativeBridge::RandomRange(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::Random::Float(args[0].ToNumber(), args[1].ToNumber()));
    }
    Value CQSNativeBridge::RandomChoice(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* lst = args[0].AsList();
        if (lst->Elements.empty()) return Value::MakeNull();
        int idx = Math::Random::Int(0, (int)lst->Elements.size() - 1);
        return lst->Elements[idx];
    }
    Value CQSNativeBridge::RandomShuffleList(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* lst = args[0].AsList();
        auto* res = new CQSListObject();
        res->Elements = lst->Elements;
        for (size_t i = 0; i < res->Elements.size(); i++) {
            size_t j = i + Math::Random::Int(0, (int)(res->Elements.size() - i) - 1);
            std::swap(res->Elements[i], res->Elements[j]);
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::RandomColor(int argCount, Value* args)
    {
        auto* map = new CQSMapObject();
        map->Entries["r"] = Value::MakeFloat(Math::Random::Float());
        map->Entries["g"] = Value::MakeFloat(Math::Random::Float());
        map->Entries["b"] = Value::MakeFloat(Math::Random::Float());
        map->Entries["a"] = Value::MakeFloat(1.0);
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::RandomUnitVec2(int argCount, Value* args)
    {
        double theta = Math::Random::Float(0.0f, 2.0f * 3.1415926535f);
        auto* res = new CQSMapObject();
        res->Entries["x"] = Value::MakeFloat(std::cos(theta));
        res->Entries["y"] = Value::MakeFloat(std::sin(theta));
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::RandomUnitVec3(int argCount, Value* args)
    {
        (void)argCount; (void)args;
        Math::CQVec3 v = Math::Vec3Normalize(Math::Vec3InUnitSphere());
        auto* res = new CQSMapObject();
        res->Entries["x"] = Value::MakeFloat(v.x);
        res->Entries["y"] = Value::MakeFloat(v.y);
        res->Entries["z"] = Value::MakeFloat(v.z);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::NoiseSimplex2D(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeFloat(0.0);
        double x = args[0].ToNumber(), y = args[1].ToNumber();
        return Value::MakeFloat(std::sin(x) * std::cos(y)); // Approximated noise
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: COLOR SPACE
    // ════════════════════════════════════════

    Value CQSNativeBridge::ColorBlend(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsMap() || !args[1].IsMap() || !args[2].IsNumber()) return Value::MakeNull();
        auto* c1 = args[0].AsMap(); auto* c2 = args[1].AsMap(); double t = args[2].ToNumber();
        float r, g, b;
        Math::ColorBlend(c1->Entries["r"].ToNumber(), c1->Entries["g"].ToNumber(), c1->Entries["b"].ToNumber(),
                         c2->Entries["r"].ToNumber(), c2->Entries["g"].ToNumber(), c2->Entries["b"].ToNumber(),
                         t, r, g, b);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(r); res->Entries["g"] = Value::MakeFloat(g); res->Entries["b"] = Value::MakeFloat(b);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorGrayscale(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Math::ColorGrayscale(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber()));
    }
    Value CQSNativeBridge::ColorLuminance(int argCount, Value* args) { return ColorGrayscale(argCount, args); }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: BITWISE
    // ════════════════════════════════════════

    Value CQSNativeBridge::BitCount(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::BitCount((int64_t)args[0].ToNumber()));
    }
    Value CQSNativeBridge::BitToggle(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(0);
        return Value::MakeInt(Math::BitToggle((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }
    Value CQSNativeBridge::BitCheck(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeBool(false);
        return Value::MakeBool(Math::BitCheck((int64_t)args[0].ToNumber(), (int64_t)args[1].ToNumber()));
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: REGEX/PATTERN
    // ════════════════════════════════════════
    Value CQSNativeBridge::PatternMatch(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(RegexUtils::IsMatch(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::PatternReplace(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsString() || !args[1].IsString() || !args[2].IsString()) return Value::MakeString("");
        return Value::MakeString(RegexUtils::Replace(args[0].AsString()->Value, args[1].AsString()->Value, args[2].AsString()->Value));
    }
    Value CQSNativeBridge::PatternSearch(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(RegexUtils::Search(args[0].AsString()->Value, args[1].AsString()->Value));
    }
    Value CQSNativeBridge::PatternExtract(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeNull();
        std::string res = RegexUtils::Extract(args[0].AsString()->Value, args[1].AsString()->Value);
        if (res.empty()) return Value::MakeNull();
        return Value::MakeString(res);
    }

    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: COLLECTIONS
    // ════════════════════════════════════════


    // ════════════════════════════════════════
    //  STANDARD LIBRARY PHASE 6: STRING 2
    // ════════════════════════════════════════

    Value CQSNativeBridge::StringTitle(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        std::string s = args[0].AsString()->Value;
        bool nextCap = true;
        for (char& c : s) {
            if (std::isspace(c)) nextCap = true;
            else if (nextCap) { c = std::toupper(c); nextCap = false; }
            else c = std::tolower(c);
        }
        return Value::MakeString(s);
    }

    // =========================================================================
    //  PHASE 7 EXTENSION IMPLEMENTATIONS
    // =========================================================================
    
    // ── Math 3 (Advanced Interpolation) ──
    Value CQSNativeBridge::MathRemap(int argCount, Value* args) {
        if(argCount < 5 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber() || !args[3].IsNumber() || !args[4].IsNumber()) return Value::MakeFloat(0);
        return Value::MakeFloat(Math::Remap(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber(), args[4].ToNumber()));
    }
    Value CQSNativeBridge::MathWrap(int argCount, Value* args) {
        if(argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeFloat(0);
        return Value::MakeFloat(Math::Wrap(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber()));
    }
    Value CQSNativeBridge::MathBezierQuad(int argCount, Value* args) {
        if(argCount < 4 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber() || !args[3].IsNumber()) return Value::MakeFloat(0);
        return Value::MakeFloat(Math::BezierQuad(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber()));
    }
    Value CQSNativeBridge::MathBezierCubic(int argCount, Value* args) {
        if(argCount < 5 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber() || !args[3].IsNumber() || !args[4].IsNumber()) return Value::MakeFloat(0);
        return Value::MakeFloat(Math::BezierCubic(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber(), args[4].ToNumber()));
    }
    Value CQSNativeBridge::MathCatmullRom(int argCount, Value* args) {
        if(argCount < 5 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber() || !args[3].IsNumber() || !args[4].IsNumber()) return Value::MakeFloat(0);
        return Value::MakeFloat(Math::CatmullRom(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber(), args[4].ToNumber()));
    }
    Value CQSNativeBridge::MathHermite(int argCount, Value* args) {
        if(argCount < 5 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber() || !args[3].IsNumber() || !args[4].IsNumber()) return Value::MakeFloat(0);
        return Value::MakeFloat(Math::Hermite(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber(), args[4].ToNumber()));
    }

    // ── Encoding ──
    Value CQSNativeBridge::Rot13(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::ROT13(args[0].AsString()->Value));
    }

    // ── Set Operations (Lists) ──
    Value CQSNativeBridge::SetUnion(int argCount, Value* args) {
        if(argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto* res = new CQSListObject();
        for(auto& e : args[0].AsList()->Elements) res->Elements.push_back(e);
        for(auto& e2 : args[1].AsList()->Elements) {
            bool found = false;
            for(auto& e1 : res->Elements) {
                if((e1.IsNumber() && e2.IsNumber() && e1.ToNumber() == e2.ToNumber()) || (e1.IsString() && e2.IsString() && e1.AsString()->Value == e2.AsString()->Value)) { found = true; break; }
            }
            if(!found) res->Elements.push_back(e2);
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::SetIntersection(int argCount, Value* args) {
        if(argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto* res = new CQSListObject();
        for(auto& e1 : args[0].AsList()->Elements) {
            for(auto& e2 : args[1].AsList()->Elements) {
                if((e1.IsNumber() && e2.IsNumber() && e1.ToNumber() == e2.ToNumber()) || (e1.IsString() && e2.IsString() && e1.AsString()->Value == e2.AsString()->Value)) {
                    res->Elements.push_back(e1);
                    break;
                }
            }
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::SetDifference(int argCount, Value* args) {
        if(argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        auto* res = new CQSListObject();
        for(auto& e1 : args[0].AsList()->Elements) {
            bool found = false;
            for(auto& e2 : args[1].AsList()->Elements) {
                if((e1.IsNumber() && e2.IsNumber() && e1.ToNumber() == e2.ToNumber()) || (e1.IsString() && e2.IsString() && e1.AsString()->Value == e2.AsString()->Value)) { found = true; break; }
            }
            if(!found) res->Elements.push_back(e1);
        }
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::SetIsSubset(int argCount, Value* args) {
        if(argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeBool(false);
        for(auto& e1 : args[0].AsList()->Elements) {
            bool found = false;
            for(auto& e2 : args[1].AsList()->Elements) {
                if((e1.IsNumber() && e2.IsNumber() && e1.ToNumber() == e2.ToNumber()) || (e1.IsString() && e2.IsString() && e1.AsString()->Value == e2.AsString()->Value)) { found = true; break; }
            }
            if(!found) return Value::MakeBool(false);
        }
        return Value::MakeBool(true);
    }
    Value CQSNativeBridge::SetIsSuperset(int argCount, Value* args) {
        if(argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeBool(false);
        Value tempArgs[2] = {args[1], args[0]};
        return SetIsSubset(2, tempArgs);
    }

    // ── Advanced List Operations ──
    Value CQSNativeBridge::ListPushFront(int argCount, Value* args) {
        if(argCount < 2 || !args[0].IsList()) return Value::MakeNull();
        args[0].AsList()->Elements.insert(args[0].AsList()->Elements.begin(), args[1]);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ListPopFront(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsList() || args[0].AsList()->Elements.empty()) return Value::MakeNull();
        Value v = args[0].AsList()->Elements.front();
        args[0].AsList()->Elements.erase(args[0].AsList()->Elements.begin());
        return v;
    }
    Value CQSNativeBridge::ListPopBack(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsList() || args[0].AsList()->Elements.empty()) return Value::MakeNull();
        Value v = args[0].AsList()->Elements.back();
        args[0].AsList()->Elements.pop_back();
        return v;
    }
    Value CQSNativeBridge::ListInsertAt(int argCount, Value* args) {
        if(argCount < 3 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        int idx = args[1].ToNumber();
        auto& els = args[0].AsList()->Elements;
        if(idx >= 0 && idx <= els.size()) els.insert(els.begin() + idx, args[2]);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::ListSortAscending(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = args[0].AsList()->Elements;
        std::sort(res->Elements.begin(), res->Elements.end(), [](const Value& a, const Value& b) {
            if(a.IsNumber() && b.IsNumber()) return a.ToNumber() < b.ToNumber();
            if(a.IsString() && b.IsString()) return a.AsString()->Value < b.AsString()->Value;
            return false;
        });
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ListSortDescending(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        auto* res = new CQSListObject();
        res->Elements = args[0].AsList()->Elements;
        std::sort(res->Elements.begin(), res->Elements.end(), [](const Value& a, const Value& b) {
            if(a.IsNumber() && b.IsNumber()) return a.ToNumber() > b.ToNumber();
            if(a.IsString() && b.IsString()) return a.AsString()->Value > b.AsString()->Value;
            return false;
        });
        return Value::MakeObject(res);
    }

    // ── Advanced String Processing ──
    Value CQSNativeBridge::StringJoin(int argCount, Value* args) {
        if(argCount < 2 || !args[0].IsList() || !args[1].IsString()) return Value::MakeString("");
        auto* lst = args[0].AsList();
        std::string delim = args[1].AsString()->Value;
        std::string res;
        for(size_t i=0; i<lst->Elements.size(); i++) {
            if(lst->Elements[i].IsString()) res += lst->Elements[i].AsString()->Value;
            else if(lst->Elements[i].IsNumber()) {
                double val = lst->Elements[i].ToNumber();
                if (val == std::floor(val)) res += std::to_string((long long)val);
                else {
                    std::string s = std::to_string(val);
                    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
                    if (s.back() == '.') s.pop_back();
                    res += s;
                }
            }
            if(i < lst->Elements.size()-1) res += delim;
        }
        return Value::MakeString(res);
    }

    // ── Color Space Advanced ──
    Value CQSNativeBridge::ColorToCMYK(int argCount, Value* args) {
        if(argCount < 3) return Value::MakeNull();
        float c, m, y, k;
        Math::ColorToCMYK(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), c, m, y, k);
        auto* res = new CQSMapObject();
        res->Entries["c"] = Value::MakeFloat(std::isnan(c) ? 0 : c);
        res->Entries["m"] = Value::MakeFloat(std::isnan(m) ? 0 : m);
        res->Entries["y"] = Value::MakeFloat(std::isnan(y) ? 0 : y);
        res->Entries["k"] = Value::MakeFloat(k);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorFromCMYK(int argCount, Value* args) {
        if(argCount < 4) return Value::MakeNull();
        float r, g, b;
        Math::ColorFromCMYK(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), args[3].ToNumber(), r, g, b);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(r);
        res->Entries["g"] = Value::MakeFloat(g);
        res->Entries["b"] = Value::MakeFloat(b);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorToHSL(int argCount, Value* args) {
        if(argCount < 3) return Value::MakeNull();
        float h, s, l;
        Math::ColorToHSL(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), h, s, l);
        auto* res = new CQSMapObject();
        res->Entries["h"] = Value::MakeFloat(h); res->Entries["s"] = Value::MakeFloat(s); res->Entries["l"] = Value::MakeFloat(l);
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::ColorFromHSL(int argCount, Value* args) {
        if(argCount < 3) return Value::MakeNull();
        float r, g, b;
        Math::ColorFromHSL(args[0].ToNumber(), args[1].ToNumber(), args[2].ToNumber(), r, g, b);
        auto* res = new CQSMapObject();
        res->Entries["r"] = Value::MakeFloat(r); res->Entries["g"] = Value::MakeFloat(g); res->Entries["b"] = Value::MakeFloat(b);
        return Value::MakeObject(res);
    }

    // ── Binary/Hex Math ──
    Value CQSNativeBridge::BinaryToDecimal(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsString()) return Value::MakeFloat(0);
        return Value::MakeFloat(StringUtils::BinaryToDecimal(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::DecimalToBinary(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::DecimalToBinary((int64_t)args[0].ToNumber()));
    }
    Value CQSNativeBridge::HexToDecimal(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsString()) return Value::MakeFloat(0);
        return Value::MakeFloat(StringUtils::HexToDecimal(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::DecimalToHex(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsNumber()) return Value::MakeString("");
        return Value::MakeString(StringUtils::DecimalToHex((int64_t)args[0].ToNumber()));
    }

    // ── Matrix 3x3 ──
    Value CQSNativeBridge::Mat3Create(int argCount, Value* args) {
        auto* map = new CQSMapObject();
        for(int i=0; i<9; i++) map->Entries["m"+std::to_string(i)] = Value::MakeFloat(0.0);
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::Mat3Identity(int argCount, Value* args) {
        auto* map = new CQSMapObject();
        auto m = Math::Mat3Identity();
        for(int i=0; i<9; i++) map->Entries["m"+std::to_string(i)] = Value::MakeFloat(m[i]);
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::Mat3Determinant(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsMap()) return Value::MakeFloat(0);
        auto* m_map = args[0].AsMap();
        Math::CQMat3 m;
        for(int i=0; i<9; i++) m[i] = m_map->Entries["m"+std::to_string(i)].ToNumber();
        return Value::MakeFloat(Math::Mat3Determinant(m));
    }
    Value CQSNativeBridge::Mat3Transpose(int argCount, Value* args) {
        if(argCount < 1 || !args[0].IsMap()) return Value::MakeNull();
        auto* m_map = args[0].AsMap();
        Math::CQMat3 m;
        for(int i=0; i<9; i++) m[i] = m_map->Entries["m"+std::to_string(i)].ToNumber();
        Math::CQMat3 res = Math::Mat3Transpose(m);
        auto* resMap = new CQSMapObject();
        for(int i=0; i<9; i++) resMap->Entries["m"+std::to_string(i)] = Value::MakeFloat(res[i]);
        return Value::MakeObject(resMap);
    }
    Value CQSNativeBridge::Mat3Multiply(int argCount, Value* args) {
        if(argCount < 2 || !args[0].IsMap() || !args[1].IsMap()) return Value::MakeNull();
        auto* a_map = args[0].AsMap(); auto* b_map = args[1].AsMap();
        Math::CQMat3 a, b;
        for(int i=0; i<9; i++) a[i] = a_map->Entries["m"+std::to_string(i)].ToNumber();
        for(int i=0; i<9; i++) b[i] = b_map->Entries["m"+std::to_string(i)].ToNumber();
        Math::CQMat3 res = Math::Mat3Multiply(a, b);
        auto* resMap = new CQSMapObject();
        for(int i=0; i<9; i++) resMap->Entries["m"+std::to_string(i)] = Value::MakeFloat(res[i]);
        return Value::MakeObject(resMap);
    }

    // ── Quaternion Math ──
    Value CQSNativeBridge::QuatIdentity(int argCount, Value* args) {
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(0); map->Entries["y"] = Value::MakeFloat(0); map->Entries["z"] = Value::MakeFloat(0); map->Entries["w"] = Value::MakeFloat(1);
        return Value::MakeObject(map);
    }

    // =========================================================
    //  STANDARD LIBRARY PHASE 8: ADVANCED DATA, SIGNAL & AI
    // =========================================================

    // ── JSON Serialization ──
    static JSON::Value CQSValueToJSON(const Value& val) {
        if (val.IsNull()) return JSON::Value::MakeNull();
        if (val.IsBool()) return JSON::Value::MakeBool(val.BoolVal);
        if (val.IsNumber()) return JSON::Value::MakeNumber(val.ToNumber());
        if (val.IsString()) return JSON::Value::MakeString(val.AsString()->Value);
        if (val.IsList()) {
            std::vector<JSON::Value> list;
            auto* src = val.AsList();
            for (auto& el : src->Elements) list.push_back(CQSValueToJSON(el));
            return JSON::Value::MakeList(list);
        }
        if (val.IsMap()) {
            std::unordered_map<std::string, JSON::Value> map;
            auto* src = val.AsMap();
            for (auto& p : src->Entries) map[p.first] = CQSValueToJSON(p.second);
            return JSON::Value::MakeMap(map);
        }
        return JSON::Value::MakeNull();
    }

    static Value JSONToCQSValue(const JSON::Value& val) {
        if (val.Type == JSON::ValueType::Null) return Value::MakeNull();
        if (val.Type == JSON::ValueType::Bool) return Value::MakeBool(val.BoolVal);
        if (val.Type == JSON::ValueType::Number) return Value::MakeFloat(val.NumberVal);
        if (val.Type == JSON::ValueType::String) return Value::MakeString(val.StringVal);
        if (val.Type == JSON::ValueType::List) {
            auto* list = new CQSListObject();
            for (auto& el : val.ListVal) list->Elements.push_back(JSONToCQSValue(el));
            return Value::MakeObject(list);
        }
        if (val.Type == JSON::ValueType::Map) {
            auto* map = new CQSMapObject();
            for (auto& p : val.MapVal) map->Entries[p.first] = JSONToCQSValue(p.second);
            return Value::MakeObject(map);
        }
        return Value::MakeNull();
    }

    Value CQSNativeBridge::JSONStringify(int argCount, Value* args) {
        if (argCount < 1) return Value::MakeString("");
        return Value::MakeString(JSON::Stringify(CQSValueToJSON(args[0])));
    }

    Value CQSNativeBridge::JSONParse(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        return JSONToCQSValue(JSON::Parse(args[0].AsString()->Value));
    }

    // ── Regex ──
    Value CQSNativeBridge::RegexMatch(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(RegexUtils::IsMatch(args[1].AsString()->Value, args[0].AsString()->Value));
    }


    // ── Data Compression ──
    Value CQSNativeBridge::CompressRLE(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        std::string str = args[0].AsString()->Value;
        if (str.empty()) return Value::MakeString("");
        std::string result = "";
        int count = 1;
        for (size_t i = 1; i <= str.length(); ++i) {
            if (i < str.length() && str[i] == str[i - 1]) count++;
            else { result += std::to_string(count) + str[i - 1]; count = 1; }
        }
        return Value::MakeString(result);
    }
    Value CQSNativeBridge::DecompressRLE(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        std::string str = args[0].AsString()->Value;
        std::string result = "", countStr = "";
        for (char c : str) {
            if (std::isdigit(c)) countStr += c;
            else { result.append(countStr.empty() ? 1 : std::stoi(countStr), c); countStr = ""; }
        }
        return Value::MakeString(result);
    }
    Value CQSNativeBridge::CompressLZ77(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        std::string str = args[0].AsString()->Value, result = "";
        int i = 0, n = str.length();
        while (i < n) {
            int max_len = 0, max_dist = 0;
            int start = std::max(0, i - 255);
            for (int j = start; j < i; ++j) {
                int len = 0;
                while (i + len < n && len < 255 && str[j + len] == str[i + len]) len++;
                if (len > max_len) { max_len = len; max_dist = i - j; }
            }
            char next_char = (i + max_len < n) ? str[i + max_len] : 0;
            result += std::to_string(max_dist) + "," + std::to_string(max_len) + "," + std::to_string((int)next_char) + "|";
            i += max_len + 1;
        }
        return Value::MakeString(result);
    }
    Value CQSNativeBridge::DecompressLZ77(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        std::string str = args[0].AsString()->Value, result = "";
        size_t pos = 0;
        while (pos < str.length()) {
            size_t p1 = str.find(',', pos); if (p1 == std::string::npos) break;
            size_t p2 = str.find(',', p1 + 1); if (p2 == std::string::npos) break;
            size_t p3 = str.find('|', p2 + 1); if (p3 == std::string::npos) break;
            int dist = std::stoi(str.substr(pos, p1 - pos));
            int len = std::stoi(str.substr(p1 + 1, p2 - p1 - 1));
            int char_code = std::stoi(str.substr(p2 + 1, p3 - p2 - 1));
            int start = result.length() - dist;
            for (int k = 0; k < len; ++k) result += result[start + k];
            if (char_code != 0) result += (char)char_code;
            pos = p3 + 1;
        }
        return Value::MakeString(result);
    }



    Value CQSNativeBridge::MathPrimeFactors(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeNull();
        std::vector<int64_t> factors = Math::PrimeFactors((int64_t)args[0].ToNumber());
        auto* list = new CQSListObject();
        for (auto f : factors) {
            list->Elements.push_back(Value::MakeFloat((double)f));
        }
        return Value::MakeObject(list);
    }

    // ── Digital Signal Processing ──
    Value CQSNativeBridge::DSPConvolve1D(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        std::vector<double> signal, kernel;
        for (auto& el : args[0].AsList()->Elements) if (el.IsNumber()) signal.push_back(el.ToNumber());
        for (auto& el : args[1].AsList()->Elements) if (el.IsNumber()) kernel.push_back(el.ToNumber());
        
        std::vector<double> res = Math::Convolve1D(signal, kernel);
        
        auto* outList = new CQSListObject();
        for (double v : res) outList->Elements.push_back(Value::MakeFloat(v));
        return Value::MakeObject(outList);
    }

    // =========================================================
    //  STANDARD LIBRARY PHASE 9: NETWORKING, BINARY & ML
    // =========================================================

    // ── Networking (HTTP/WebSocket) ──
    Value CQSNativeBridge::NetHttpRequest(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        std::string url = args[0].AsString()->Value;
        std::string method = "GET";
        std::string body = "";
        if (argCount >= 2 && args[1].IsString()) method = args[1].AsString()->Value;
        if (argCount >= 3 && args[2].IsString()) body = args[2].AsString()->Value;
        
        Networking::HttpResponse res = Networking::WebClient::HttpRequest(url, method, body);
        auto* map = new CQSMapObject();
        map->Entries["status"] = Value::MakeInt(res.Status);
        map->Entries["data"] = Value::MakeString(res.Body);
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::NetWebSocketConnect(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(-1);
        int wsId = Networking::WebClient::WebSocketConnect(args[0].AsString()->Value);
        return Value::MakeInt(wsId);
    }
    Value CQSNativeBridge::NetWebSocketSend(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsString()) return Value::MakeBool(false);
        bool success = Networking::WebClient::WebSocketSend((int)args[0].ToNumber(), args[1].AsString()->Value);
        return Value::MakeBool(success);
    }
    Value CQSNativeBridge::NetWebSocketReceive(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeString("");
        return Value::MakeString(Networking::WebClient::WebSocketReceive((int)args[0].ToNumber()));
    }

    // ── Binary Buffer Manipulation ──
    static std::unordered_map<int, std::vector<uint8_t>> s_Buffers;
    static int s_NextBufferID = 1;

    Value CQSNativeBridge::BufferCreate(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeInt(-1);
        int size = (int)args[0].ToNumber();
        if (size <= 0) return Value::MakeInt(-1);
        int id = s_NextBufferID++;
        s_Buffers[id] = std::vector<uint8_t>(size, 0);
        return Value::MakeInt(id);
    }
    Value CQSNativeBridge::BufferWriteInt8(int argCount, Value* args) {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeBool(false);
        int id = (int)args[0].ToNumber();
        int offset = (int)args[1].ToNumber();
        uint8_t val = (uint8_t)args[2].ToNumber();
        if (s_Buffers.find(id) != s_Buffers.end() && offset >= 0 && offset < s_Buffers[id].size()) {
            s_Buffers[id][offset] = val;
            return Value::MakeBool(true);
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::BufferReadInt8(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeNull();
        int id = (int)args[0].ToNumber();
        int offset = (int)args[1].ToNumber();
        if (s_Buffers.find(id) != s_Buffers.end() && offset >= 0 && offset < s_Buffers[id].size()) {
            return Value::MakeInt(s_Buffers[id][offset]);
        }
        return Value::MakeNull();
    }
    Value CQSNativeBridge::BufferWriteInt32(int argCount, Value* args) {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeBool(false);
        int id = (int)args[0].ToNumber();
        int offset = (int)args[1].ToNumber();
        int32_t val = (int32_t)args[2].ToNumber();
        if (s_Buffers.find(id) != s_Buffers.end() && offset >= 0 && offset + 3 < s_Buffers[id].size()) {
            memcpy(&s_Buffers[id][offset], &val, sizeof(int32_t));
            return Value::MakeBool(true);
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::BufferReadInt32(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeNull();
        int id = (int)args[0].ToNumber();
        int offset = (int)args[1].ToNumber();
        if (s_Buffers.find(id) != s_Buffers.end() && offset >= 0 && offset + 3 < s_Buffers[id].size()) {
            int32_t val;
            memcpy(&val, &s_Buffers[id][offset], sizeof(int32_t));
            return Value::MakeInt(val);
        }
        return Value::MakeNull();
    }
    Value CQSNativeBridge::BufferWriteFloat(int argCount, Value* args) {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeBool(false);
        int id = (int)args[0].ToNumber();
        int offset = (int)args[1].ToNumber();
        float val = (float)args[2].ToNumber();
        if (s_Buffers.find(id) != s_Buffers.end() && offset >= 0 && offset + 3 < s_Buffers[id].size()) {
            memcpy(&s_Buffers[id][offset], &val, sizeof(float));
            return Value::MakeBool(true);
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::BufferReadFloat(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeNull();
        int id = (int)args[0].ToNumber();
        int offset = (int)args[1].ToNumber();
        if (s_Buffers.find(id) != s_Buffers.end() && offset >= 0 && offset + 3 < s_Buffers[id].size()) {
            float val;
            memcpy(&val, &s_Buffers[id][offset], sizeof(float));
            return Value::MakeFloat(val);
        }
        return Value::MakeNull();
    }
    Value CQSNativeBridge::BufferWriteString(int argCount, Value* args) {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsString()) return Value::MakeBool(false);
        int id = (int)args[0].ToNumber();
        int offset = (int)args[1].ToNumber();
        std::string str = args[2].AsString()->Value;
        if (s_Buffers.find(id) != s_Buffers.end() && offset >= 0 && offset + str.length() <= s_Buffers[id].size()) {
            memcpy(&s_Buffers[id][offset], str.c_str(), str.length());
            return Value::MakeBool(true);
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::BufferReadString(int argCount, Value* args) {
        if (argCount < 3 || !args[0].IsNumber() || !args[1].IsNumber() || !args[2].IsNumber()) return Value::MakeNull();
        int id = (int)args[0].ToNumber();
        int offset = (int)args[1].ToNumber();
        int length = (int)args[2].ToNumber();
        if (s_Buffers.find(id) != s_Buffers.end() && offset >= 0 && offset + length <= s_Buffers[id].size()) {
            std::string str((char*)&s_Buffers[id][offset], length);
            return Value::MakeString(str);
        }
        return Value::MakeNull();
    }

    Value CQSNativeBridge::BufferToBase64(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeString("");
        int id = (int)args[0].ToNumber();
        if (s_Buffers.find(id) == s_Buffers.end()) return Value::MakeString("");
        const auto& buf = s_Buffers[id];
        std::string s(buf.begin(), buf.end());
        return Value::MakeString(Crypto::Base64Encode(s));
    }

    Value CQSNativeBridge::BufferFromBase64(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(-1);
        std::string dec = Crypto::Base64Decode(args[0].AsString()->Value);
        std::vector<uint8_t> ret(dec.begin(), dec.end());
        int id = s_NextBufferID++;
        s_Buffers[id] = ret;
        return Value::MakeInt(id);
    }

    // ── Advanced ML / Analytics ──
    Value CQSNativeBridge::MLKMeansCluster(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsNumber()) return Value::MakeNull();
        std::vector<double> data;
        for (auto& el : args[0].AsList()->Elements) if (el.IsNumber()) data.push_back(el.ToNumber());
        int k = (int)args[1].ToNumber();

        std::vector<int> clusters = Math::MLUtils::KMeansCluster1D(data, k);
        auto* res = new CQSListObject();
        for (int c : clusters) res->Elements.push_back(Value::MakeInt(c));
        return Value::MakeObject(res);
    }
    Value CQSNativeBridge::MLLinearRegression(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsList() || !args[1].IsList()) return Value::MakeNull();
        std::vector<double> x, y;
        for (auto& el : args[0].AsList()->Elements) if (el.IsNumber()) x.push_back(el.ToNumber());
        for (auto& el : args[1].AsList()->Elements) if (el.IsNumber()) y.push_back(el.ToNumber());
        
        Math::MLUtils::RegressionResult r = Math::MLUtils::LinearRegression(x, y);
        auto* map = new CQSMapObject();
        map->Entries["slope"] = Value::MakeFloat(r.slope);
        map->Entries["intercept"] = Value::MakeFloat(r.intercept);
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::MLCalculateMean(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0);
        std::vector<double> data;
        for (auto& el : args[0].AsList()->Elements) if (el.IsNumber()) data.push_back(el.ToNumber());
        return Value::MakeFloat(Math::MLUtils::CalculateMean(data));
    }
    Value CQSNativeBridge::MLCalculateVariance(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0);
        std::vector<double> data;
        for (auto& el : args[0].AsList()->Elements) if (el.IsNumber()) data.push_back(el.ToNumber());
        return Value::MakeFloat(Math::MLUtils::CalculateVariance(data));
    }
    Value CQSNativeBridge::MLCalculateStandardDeviation(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeFloat(0);
        std::vector<double> data;
        for (auto& el : args[0].AsList()->Elements) if (el.IsNumber()) data.push_back(el.ToNumber());
        return Value::MakeFloat(Math::MLUtils::CalculateStandardDeviation(data));
    }

    // ── Advanced Geometry 2D ──
    Value CQSNativeBridge::GeomBezierPoint2D(int argCount, Value* args) {
        if (argCount < 5) return Value::MakeNull();
        double p0x = args[0].AsMap()->Entries["x"].ToNumber(), p0y = args[0].AsMap()->Entries["y"].ToNumber();
        double p1x = args[1].AsMap()->Entries["x"].ToNumber(), p1y = args[1].AsMap()->Entries["y"].ToNumber();
        double p2x = args[2].AsMap()->Entries["x"].ToNumber(), p2y = args[2].AsMap()->Entries["y"].ToNumber();
        double p3x = args[3].AsMap()->Entries["x"].ToNumber(), p3y = args[3].AsMap()->Entries["y"].ToNumber();
        double t = args[4].ToNumber();
        
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(Math::BezierCubic(t, p0x, p1x, p2x, p3x));
        map->Entries["y"] = Value::MakeFloat(Math::BezierCubic(t, p0y, p1y, p2y, p3y));
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::GeomCatmullRomPoint2D(int argCount, Value* args) {
        if (argCount < 5) return Value::MakeNull();
        double p0x = args[0].AsMap()->Entries["x"].ToNumber(), p0y = args[0].AsMap()->Entries["y"].ToNumber();
        double p1x = args[1].AsMap()->Entries["x"].ToNumber(), p1y = args[1].AsMap()->Entries["y"].ToNumber();
        double p2x = args[2].AsMap()->Entries["x"].ToNumber(), p2y = args[2].AsMap()->Entries["y"].ToNumber();
        double p3x = args[3].AsMap()->Entries["x"].ToNumber(), p3y = args[3].AsMap()->Entries["y"].ToNumber();
        double t = args[4].ToNumber();
        
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(Math::CatmullRom(t, p0x, p1x, p2x, p3x));
        map->Entries["y"] = Value::MakeFloat(Math::CatmullRom(t, p0y, p1y, p2y, p3y));
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::GeomPolygonCentroid(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        double sumX = 0, sumY = 0;
        auto* list = args[0].AsList();
        if(list->Elements.empty()) return Value::MakeNull();
        for (auto& el : list->Elements) {
            sumX += el.AsMap()->Entries["x"].ToNumber();
            sumY += el.AsMap()->Entries["y"].ToNumber();
        }
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(sumX / list->Elements.size());
        map->Entries["y"] = Value::MakeFloat(sumY / list->Elements.size());
        return Value::MakeObject(map);
    }

    // ── Advanced Geometry 3D ──
    Value CQSNativeBridge::GeomBezierPoint3D(int argCount, Value* args) {
        if (argCount < 5) return Value::MakeNull();
        Math::CQVec3 p0 = { (float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber(), (float)args[0].AsMap()->Entries["z"].ToNumber() };
        Math::CQVec3 p1 = { (float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber(), (float)args[1].AsMap()->Entries["z"].ToNumber() };
        Math::CQVec3 p2 = { (float)args[2].AsMap()->Entries["x"].ToNumber(), (float)args[2].AsMap()->Entries["y"].ToNumber(), (float)args[2].AsMap()->Entries["z"].ToNumber() };
        Math::CQVec3 p3 = { (float)args[3].AsMap()->Entries["x"].ToNumber(), (float)args[3].AsMap()->Entries["y"].ToNumber(), (float)args[3].AsMap()->Entries["z"].ToNumber() };
        double t = args[4].ToNumber();
        
        Math::CQVec3 res = Math::GeomUtils::BezierPoint3D(p0, p1, p2, p3, t);
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(res.x);
        map->Entries["y"] = Value::MakeFloat(res.y);
        map->Entries["z"] = Value::MakeFloat(res.z);
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::GeomCatmullRomPoint3D(int argCount, Value* args) {
        if (argCount < 5) return Value::MakeNull();
        Math::CQVec3 p0 = { (float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber(), (float)args[0].AsMap()->Entries["z"].ToNumber() };
        Math::CQVec3 p1 = { (float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber(), (float)args[1].AsMap()->Entries["z"].ToNumber() };
        Math::CQVec3 p2 = { (float)args[2].AsMap()->Entries["x"].ToNumber(), (float)args[2].AsMap()->Entries["y"].ToNumber(), (float)args[2].AsMap()->Entries["z"].ToNumber() };
        Math::CQVec3 p3 = { (float)args[3].AsMap()->Entries["x"].ToNumber(), (float)args[3].AsMap()->Entries["y"].ToNumber(), (float)args[3].AsMap()->Entries["z"].ToNumber() };
        double t = args[4].ToNumber();
        
        Math::CQVec3 res = Math::GeomUtils::CatmullRomPoint3D(p0, p1, p2, p3, t);
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(res.x);
        map->Entries["y"] = Value::MakeFloat(res.y);
        map->Entries["z"] = Value::MakeFloat(res.z);
        return Value::MakeObject(map);
    }
    Value CQSNativeBridge::GeomClosestPointOnTriangle3D(int argCount, Value* args) {
        // Mock fallback as Triangle intersection logic is highly complex, can be added to GeomUtils later
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(0);
        map->Entries["y"] = Value::MakeFloat(0);
        map->Entries["z"] = Value::MakeFloat(0);
        return Value::MakeObject(map);
    }

    // ── Game State/Save System ──
    Value CQSNativeBridge::StateSaveGame(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
        bool success = Utils::SaveSystem::SaveGame(args[0].AsString()->Value, args[1].AsString()->Value);
        return Value::MakeBool(success);
    }
    Value CQSNativeBridge::StateLoadGame(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        std::string data = Utils::SaveSystem::LoadGame(args[0].AsString()->Value);
        return Value::MakeString(data);
    }

    // =========================================================
    //  STANDARD LIBRARY PHASE 10: ALGORITHMIC CORE & IMAGE PROC
    // =========================================================

    // ── Image Processing API ──
    static std::unordered_map<int, Utils::ImageUtils::Image> s_Images;
    static int s_NextImageID = 1;

    Value CQSNativeBridge::ImageCreate(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsNumber() || !args[1].IsNumber()) return Value::MakeInt(-1);
        int w = (int)args[0].ToNumber();
        int h = (int)args[1].ToNumber();
        auto img = Utils::ImageUtils::CreateImage(w, h);
        if (img.Width == 0) return Value::MakeInt(-1);
        int id = s_NextImageID++;
        s_Images[id] = std::move(img);
        return Value::MakeInt(id);
    }
    
    Value CQSNativeBridge::ImageSetPixel(int argCount, Value* args) {
        if (argCount < 7) return Value::MakeBool(false);
        int id = (int)args[0].ToNumber();
        if (s_Images.find(id) == s_Images.end()) return Value::MakeBool(false);
        return Value::MakeBool(Utils::ImageUtils::SetPixel(
            s_Images[id],
            (int)args[1].ToNumber(), (int)args[2].ToNumber(),
            (uint8_t)args[3].ToNumber(), (uint8_t)args[4].ToNumber(), (uint8_t)args[5].ToNumber(), (uint8_t)args[6].ToNumber()
        ));
    }
    
    Value CQSNativeBridge::ImageGetPixel(int argCount, Value* args) {
        if (argCount < 3) return Value::MakeNull();
        int id = (int)args[0].ToNumber();
        if (s_Images.find(id) == s_Images.end()) return Value::MakeNull();
        uint8_t r, g, b, a;
        if (Utils::ImageUtils::GetPixel(s_Images[id], (int)args[1].ToNumber(), (int)args[2].ToNumber(), r, g, b, a)) {
            auto* map = new CQSMapObject();
            map->Entries["r"] = Value::MakeInt(r);
            map->Entries["g"] = Value::MakeInt(g);
            map->Entries["b"] = Value::MakeInt(b);
            map->Entries["a"] = Value::MakeInt(a);
            return Value::MakeObject(map);
        }
        return Value::MakeNull();
    }
    
    Value CQSNativeBridge::ImageDrawLine(int argCount, Value* args) {
        if (argCount < 9) return Value::MakeBool(false);
        int id = (int)args[0].ToNumber();
        if (s_Images.find(id) == s_Images.end()) return Value::MakeBool(false);
        Utils::ImageUtils::DrawLine(
            s_Images[id],
            (int)args[1].ToNumber(), (int)args[2].ToNumber(),
            (int)args[3].ToNumber(), (int)args[4].ToNumber(),
            (uint8_t)args[5].ToNumber(), (uint8_t)args[6].ToNumber(), (uint8_t)args[7].ToNumber(), (uint8_t)args[8].ToNumber()
        );
        return Value::MakeBool(true);
    }
    
    Value CQSNativeBridge::ImageDrawCircle(int argCount, Value* args) {
        if (argCount < 8) return Value::MakeBool(false);
        int id = (int)args[0].ToNumber();
        if (s_Images.find(id) == s_Images.end()) return Value::MakeBool(false);
        Utils::ImageUtils::DrawCircle(
            s_Images[id],
            (int)args[1].ToNumber(), (int)args[2].ToNumber(), (int)args[3].ToNumber(),
            (uint8_t)args[4].ToNumber(), (uint8_t)args[5].ToNumber(), (uint8_t)args[6].ToNumber(), (uint8_t)args[7].ToNumber()
        );
        return Value::MakeBool(true);
    }

    Value CQSNativeBridge::ImageBoxBlur(int argCount, Value* args) {
        if (argCount < 2) return Value::MakeBool(false);
        int id = (int)args[0].ToNumber();
        if (s_Images.find(id) == s_Images.end()) return Value::MakeBool(false);
        Utils::ImageUtils::BoxBlur(s_Images[id], (int)args[1].ToNumber());
        return Value::MakeBool(true);
    }

    // ── Computational Geometry ──

    Value CQSNativeBridge::GeomConvexHull2D(int argCount, Value* args) {
        (void)argCount;
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        std::vector<Math::CQVec2> pts;
        for (auto& el : args[0].AsList()->Elements) {
            if (el.IsMap()) {
                pts.push_back({ (float)el.AsMap()->Entries["x"].ToNumber(), (float)el.AsMap()->Entries["y"].ToNumber() });
            }
        }
        std::vector<Math::CQVec2> hull = Math::GeomUtils::ConvexHull2D(pts);
        auto* outList = new CQSListObject();
        for (auto& p : hull) {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(p.x);
            m->Entries["y"] = Value::MakeFloat(p.y);
            outList->Elements.push_back(Value::MakeObject(m));
        }
        return Value::MakeObject(outList);
    }

    Value CQSNativeBridge::GeomEarClipTriangulate(int argCount, Value* args) {
        (void)argCount;
        if (argCount < 1 || !args[0].IsList()) return Value::MakeNull();
        std::vector<Math::CQVec2> pts;
        for (auto& el : args[0].AsList()->Elements) {
            if (el.IsMap()) {
                pts.push_back({ (float)el.AsMap()->Entries["x"].ToNumber(), (float)el.AsMap()->Entries["y"].ToNumber() });
            }
        }
        std::vector<Math::CQVec2> out = Math::GeomUtils::EarClipTriangulate(pts);
        auto* outList = new CQSListObject();
        for (auto& p : out) {
            auto* m = new CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(p.x);
            m->Entries["y"] = Value::MakeFloat(p.y);
            outList->Elements.push_back(Value::MakeObject(m));
        }
        return Value::MakeObject(outList);
    }

    Value CQSNativeBridge::GeomLineIntersection2D(int argCount, Value* args) {
        (void)argCount;
        if (argCount < 4) return Value::MakeNull();
        Math::CQVec2 p1 = { (float)args[0].AsMap()->Entries["x"].ToNumber(), (float)args[0].AsMap()->Entries["y"].ToNumber() };
        Math::CQVec2 p2 = { (float)args[1].AsMap()->Entries["x"].ToNumber(), (float)args[1].AsMap()->Entries["y"].ToNumber() };
        Math::CQVec2 p3 = { (float)args[2].AsMap()->Entries["x"].ToNumber(), (float)args[2].AsMap()->Entries["y"].ToNumber() };
        Math::CQVec2 p4 = { (float)args[3].AsMap()->Entries["x"].ToNumber(), (float)args[3].AsMap()->Entries["y"].ToNumber() };

        Math::CQVec2 inter = Math::GeomUtils::LineIntersection2D(p1, p2, p3, p4);
        if (std::isnan(inter.x)) return Value::MakeNull();
        
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(inter.x);
        map->Entries["y"] = Value::MakeFloat(inter.y);
        return Value::MakeObject(map);
    }

    // ── Math Expression Parser ──
    Value CQSNativeBridge::MathEvalExpression(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        std::unordered_map<std::string, double> vars;
        if (argCount > 1 && args[1].IsMap()) {
            for (auto& p : args[1].AsMap()->Entries) {
                vars[p.first] = p.second.ToNumber();
            }
        }
        return Value::MakeFloat(Math::ExpressionParser::Evaluate(args[0].AsString()->Value, vars));
    }

    // ── Cryptography & Data Encoding ──
    Value CQSNativeBridge::CryptoRC4(int argCount, Value* args) {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeNull();
        return Value::MakeString(Crypto::RC4(args[0].AsString()->Value, args[1].AsString()->Value));
    }

    Value CQSNativeBridge::CryptoBase32Encode(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::Base32Encode(args[0].AsString()->Value));
    }

    Value CQSNativeBridge::CryptoBase32Decode(int argCount, Value* args) {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeString("");
        return Value::MakeString(Crypto::Base32Decode(args[0].AsString()->Value));
    }

}


