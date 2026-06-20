#pragma once

#include "CQSValue.h"
#include <string>
#include <vector>

namespace Conqueror::CQS
{
    class CQSVM;

    class CQSNativeBridge
    {
    public:
        // Engine fonksiyonlarını VM'e kaydet
        static void RegisterGlobals(CQSVM* vm);

    private:
        // ── Logging ──
        static Value LogInfo(int argCount, Value* args);
        static Value LogWarn(int argCount, Value* args);
        static Value LogError(int argCount, Value* args);

        // ── Transform ──
        static Value GetPosition(int argCount, Value* args);
        static Value SetPosition(int argCount, Value* args);
        static Value GetRotation(int argCount, Value* args);
        static Value SetRotation(int argCount, Value* args);
        static Value GetScale(int argCount, Value* args);
        static Value SetScale(int argCount, Value* args);
        static Value GetForward(int argCount, Value* args);
        static Value GetRight(int argCount, Value* args);
        static Value GetUp(int argCount, Value* args);

        // ── Physics 3D ──
        static Value GetLinearVelocity(int argCount, Value* args);
        static Value SetLinearVelocity(int argCount, Value* args);
        static Value GetAngularVelocity(int argCount, Value* args);
        static Value SetAngularVelocity(int argCount, Value* args);
        static Value ApplyForce(int argCount, Value* args);
        static Value ApplyImpulse(int argCount, Value* args);
        static Value ApplyTorque(int argCount, Value* args);

        // ── Physics 2D ──
        static Value GetLinearVelocity2D(int argCount, Value* args);
        static Value SetLinearVelocity2D(int argCount, Value* args);
        static Value GetAngularVelocity2D(int argCount, Value* args);
        static Value SetAngularVelocity2D(int argCount, Value* args);
        static Value ApplyForce2D(int argCount, Value* args);
        static Value ApplyImpulse2D(int argCount, Value* args);

        // ── Input: Keyboard ──
        static Value IsKeyPressed(int argCount, Value* args);
        static Value IsKeyDown(int argCount, Value* args);
        static Value IsKeyUp(int argCount, Value* args);

        // ── Input: Mouse ──
        static Value IsMouseButtonPressed(int argCount, Value* args);
        static Value IsMouseButtonDown(int argCount, Value* args);
        static Value IsMouseButtonUp(int argCount, Value* args);
        static Value GetMousePosition(int argCount, Value* args);
        static Value GetMouseX(int argCount, Value* args);
        static Value GetMouseY(int argCount, Value* args);
        static Value GetMouseDelta(int argCount, Value* args);
        static Value GetMouseScrollDelta(int argCount, Value* args);
        static Value SetCursorMode(int argCount, Value* args);

        // ── Input: Gamepad ──
        static Value IsGamepadConnected(int argCount, Value* args);
        static Value IsGamepadButtonPressed(int argCount, Value* args);
        static Value GetGamepadAxis(int argCount, Value* args);
        static Value GetGamepadLeftStick(int argCount, Value* args);
        static Value GetGamepadRightStick(int argCount, Value* args);
        static Value GetGamepadLeftTrigger(int argCount, Value* args);
        static Value GetGamepadRightTrigger(int argCount, Value* args);
        static Value SetGamepadVibration(int argCount, Value* args);

        // ── Input: Advanced ──
        static Value InputCreateContext(int argCount, Value* args);
        static Value InputSwitchContext(int argCount, Value* args);
        static Value InputPushContext(int argCount, Value* args);
        static Value InputPopContext(int argCount, Value* args);
        static Value InputBindAction(int argCount, Value* args);
        static Value InputBindChord(int argCount, Value* args);
        static Value InputGetAction(int argCount, Value* args);
        static Value InputGetActionState(int argCount, Value* args);
        static Value InputGetActionAxis1D(int argCount, Value* args);
        static Value InputGetActionAxis2D(int argCount, Value* args);
        static Value InputIsComboTriggered(int argCount, Value* args);
        static Value InputGetAnyKey(int argCount, Value* args);

        // ── Audio ──
        static Value AudioPlay(int argCount, Value* args);
        static Value AudioStop(int argCount, Value* args);
        static Value AudioPause(int argCount, Value* args);
        static Value AudioSetVolume(int argCount, Value* args);
        static Value AudioGetVolume(int argCount, Value* args);
        static Value AudioSetPitch(int argCount, Value* args);
        static Value AudioSetLoop(int argCount, Value* args);
        static Value AudioIsPlaying(int argCount, Value* args);
        static Value AudioSetMasterVolume(int argCount, Value* args);
        static Value AudioGetMasterVolume(int argCount, Value* args);

        // ── Time ──
        static Value GetDeltaTime(int argCount, Value* args);
        static Value GetUnscaledDeltaTime(int argCount, Value* args);
        static Value GetTime(int argCount, Value* args);
        static Value GetFrameCount(int argCount, Value* args);
        static Value GetTimeScale(int argCount, Value* args);
        static Value SetTimeScale(int argCount, Value* args);

        // ── Scene / Entity ──
        static Value FindEntityByTag(int argCount, Value* args);
        static Value GetEntityTag(int argCount, Value* args);
        static Value SetEntityTag(int argCount, Value* args);
        static Value DestroyEntity(int argCount, Value* args);
        static Value CreateEntity(int argCount, Value* args);
        static Value GetEntityName(int argCount, Value* args);

        // ── Component Queries ──
        static Value HasRigidbody(int argCount, Value* args);
        static Value HasAudioSource(int argCount, Value* args);
        static Value HasAnimator(int argCount, Value* args);
        static Value HasCamera(int argCount, Value* args);

        // ── Animator ──
        static Value AnimatorPlay(int argCount, Value* args);
        static Value AnimatorStop(int argCount, Value* args);
        static Value AnimatorSetSpeed(int argCount, Value* args);
        static Value AnimatorGetSpeed(int argCount, Value* args);
        static Value AnimatorSetClip(int argCount, Value* args);

        // ── Renderer ──
        static Value SetColor(int argCount, Value* args);
        static Value GetColor(int argCount, Value* args);
        static Value SetVisible(int argCount, Value* args);

        // ── Camera ──
        static Value GetCameraFOV(int argCount, Value* args);
        static Value SetCameraFOV(int argCount, Value* args);

        // ── Math Helpers ──
        static Value MathSin(int argCount, Value* args);
        static Value MathCos(int argCount, Value* args);
        static Value MathTan(int argCount, Value* args);
        static Value MathSqrt(int argCount, Value* args);
        static Value MathAbs(int argCount, Value* args);
        static Value MathFloor(int argCount, Value* args);
        static Value MathCeil(int argCount, Value* args);
        static Value MathClamp(int argCount, Value* args);
        static Value MathLerp(int argCount, Value* args);
        static Value MathMin(int argCount, Value* args);
        static Value MathMax(int argCount, Value* args);
        static Value MathPow(int argCount, Value* args);
        static Value MathRandom(int argCount, Value* args);

        // ── Advanced Math ──
        static Value MathRGBToHSL(int argCount, Value* args);
        static Value MathHSLToRGB(int argCount, Value* args);
        static Value MathEvaluateEasing(int argCount, Value* args);
        static Value MathRayCapsuleIntersect(int argCount, Value* args);
        static Value MathComputeMinimalBoundingSphere(int argCount, Value* args);
        static Value MathRunThreadSafetyTest(int argCount, Value* args);

        // ── Logging ──
        static Value LogRegisterCategory(int argCount, Value* args);
        static Value LogSetLevel(int argCount, Value* args);
        static Value LogGenerateCrashDump(int argCount, Value* args);
        static Value LogGetHistoryCount(int argCount, Value* args);
        static Value LogJSON(int argCount, Value* args);
        static Value LogPushContext(int argCount, Value* args);
        static Value LogPopContext(int argCount, Value* args);
        static Value LogSendWebhookAlert(int argCount, Value* args);
        static Value LogEnableNetworkBroadcaster(int argCount, Value* args);
        static Value LogMetricIncrement(int argCount, Value* args);
        static Value LogMetricSetGauge(int argCount, Value* args);
        static Value LogMetricRecordTime(int argCount, Value* args);
        static Value LogDumpMetricsToLog(int argCount, Value* args);
        static Value LogSetCategoryEnabled(int argCount, Value* args);
        static Value LogIsCategoryEnabled(int argCount, Value* args);
        static Value LogSetGlobalLevel(int argCount, Value* args);
        static Value LogGetGlobalLevel(int argCount, Value* args);
        static Value LogExportHistoryToHtml(int argCount, Value* args);
        static Value LogExportHistoryToJson(int argCount, Value* args);
        static Value LogExportProfilerSession(int argCount, Value* args);
        static Value LogClearLogHistory(int argCount, Value* args);
        static Value LogFlushAll(int argCount, Value* args);
        static Value LogDisableNetworkBroadcaster(int argCount, Value* args);
        static Value LogGetCurrentContext(int argCount, Value* args);

        // ── DebugDraw ──
        static Value DebugDrawLine(int argCount, Value* args);
        static Value DebugDrawBox(int argCount, Value* args);
        static Value DebugDrawSolidBox(int argCount, Value* args);
        static Value DebugDrawSphere(int argCount, Value* args);
        static Value DebugDrawSolidSphere(int argCount, Value* args);
        static Value DebugDrawText3D(int argCount, Value* args);
        static Value DebugDrawRay(int argCount, Value* args);
        static Value DebugDrawCapsule(int argCount, Value* args);
        static Value DebugDrawGrid(int argCount, Value* args);
        static Value DebugDrawCoordinateAxes(int argCount, Value* args);
        static Value DebugDrawVelocity(int argCount, Value* args);
        static Value DebugDrawClear(int argCount, Value* args);
        static Value DebugDrawClearTimed(int argCount, Value* args);
        static Value DebugDrawSetEnabled(int argCount, Value* args);

        // ── Vec3 Helpers ──
        static Value Vec3Length(int argCount, Value* args);
        static Value Vec3Normalize(int argCount, Value* args);
        static Value Vec3Dot(int argCount, Value* args);
        static Value Vec3Cross(int argCount, Value* args);
        static Value Vec3Distance(int argCount, Value* args);
        static Value MakeVec3(int argCount, Value* args);
        static Value MakeVec2(int argCount, Value* args);

        // ── Navigation ──
        static Value NavSetDestination(int argCount, Value* args);
        static Value NavBake(int argCount, Value* args);
        static Value NavStop(int argCount, Value* args);
        static Value NavIsStopped(int argCount, Value* args);
        static Value NavHasPath(int argCount, Value* args);
        static Value NavGetSpeed(int argCount, Value* args);
        static Value NavSetSpeed(int argCount, Value* args);

        // ── Networking ──
        static Value NetStartServer(int argCount, Value* args);
        static Value NetConnect(int argCount, Value* args);
        static Value NetDisconnect(int argCount, Value* args);
        static Value NetSend(int argCount, Value* args);
        static Value NetBroadcast(int argCount, Value* args);
        static Value NetIsServer(int argCount, Value* args);
        static Value NetIsClient(int argCount, Value* args);
        static Value NetIsConnected(int argCount, Value* args);
        static Value NetGetPeerCount(int argCount, Value* args);

        // ── Networking (Advanced) ──
        static Value NetEnableEncryption(int argCount, Value* args);
        static Value NetIsEncryptionEnabled(int argCount, Value* args);
        static Value NetOpenNATPort(int argCount, Value* args);
        static Value NetCloseNATPort(int argCount, Value* args);
        static Value NetGetExternalIP(int argCount, Value* args);
        static Value NetCallRPC(int argCount, Value* args);

        // ── Standard Library: String ──
        static Value StringLength(int argCount, Value* args);
        static Value StringSubstring(int argCount, Value* args);
        static Value StringToUpper(int argCount, Value* args);
        static Value StringToLower(int argCount, Value* args);
        static Value StringIndexOf(int argCount, Value* args);
        static Value StringReplace(int argCount, Value* args);
        static Value StringSplit(int argCount, Value* args);
        static Value StringTrim(int argCount, Value* args);
        static Value StringStartsWith(int argCount, Value* args);
        static Value StringEndsWith(int argCount, Value* args);
        static Value StringContains(int argCount, Value* args);
        static Value StringPadLeft(int argCount, Value* args);
        static Value StringPadRight(int argCount, Value* args);
        static Value StringIsAlpha(int argCount, Value* args);
        static Value StringIsDigit(int argCount, Value* args);
        static Value StringIsAlnum(int argCount, Value* args);
        static Value StringReverse(int argCount, Value* args);

        // ── Standard Library: List ──
        static Value ListAdd(int argCount, Value* args);
        static Value ListRemoveAt(int argCount, Value* args);
        static Value ListInsert(int argCount, Value* args);
        static Value ListClear(int argCount, Value* args);
        static Value ListSize(int argCount, Value* args);
        static Value ListContains(int argCount, Value* args);
        static Value ListIndexOf(int argCount, Value* args);
        static Value ListJoin(int argCount, Value* args);
        static Value ListReverse(int argCount, Value* args);
        static Value ListShuffle(int argCount, Value* args);
        static Value ListSwap(int argCount, Value* args);
        static Value ListCount(int argCount, Value* args);
        static Value ListMin(int argCount, Value* args);
        static Value ListMax(int argCount, Value* args);
        static Value ListSum(int argCount, Value* args);
        static Value ListAverage(int argCount, Value* args);

        // ── Standard Library: Map ──
        static Value MapSet(int argCount, Value* args);
        static Value MapGet(int argCount, Value* args);
        static Value MapHasKey(int argCount, Value* args);
        static Value MapRemove(int argCount, Value* args);
        static Value MapKeys(int argCount, Value* args);
        static Value MapValues(int argCount, Value* args);
        static Value MapSize(int argCount, Value* args);
        static Value MapClear(int argCount, Value* args);

        // ── Standard Library: Extended Math ──
        static Value MathRandomInt(int argCount, Value* args);
        static Value MathRandomRange(int argCount, Value* args);
        static Value MathSign(int argCount, Value* args);
        static Value MathRound(int argCount, Value* args);
        static Value MathTrunc(int argCount, Value* args);
        static Value MathLog(int argCount, Value* args);
        static Value MathLog10(int argCount, Value* args);
        static Value MathExp(int argCount, Value* args);
        static Value MathAsin(int argCount, Value* args);
        static Value MathAcos(int argCount, Value* args);
        static Value MathAtan(int argCount, Value* args);
        static Value MathAtan2(int argCount, Value* args);
        static Value MathSinh(int argCount, Value* args);
        static Value MathCosh(int argCount, Value* args);
        static Value MathTanh(int argCount, Value* args);

        // ── Standard Library: Crypto & Encode ──
        static Value Base64Encode(int argCount, Value* args);
        static Value Base64Decode(int argCount, Value* args);
        static Value HashMD5Fallback(int argCount, Value* args);

        // ── Standard Library Phase 2: String Extras ──
        static Value StringCount(int argCount, Value* args);
        static Value StringRepeat(int argCount, Value* args);
        static Value StringCapitalize(int argCount, Value* args);
        static Value StringCharAt(int argCount, Value* args);
        static Value StringCharCodeAt(int argCount, Value* args);
        static Value StringFromCharCode(int argCount, Value* args);
        static Value StringTrimLeft(int argCount, Value* args);
        static Value StringTrimRight(int argCount, Value* args);
        static Value StringLastIndexOf(int argCount, Value* args);

        // ── Standard Library Phase 2: List Extras ──
        static Value ListSort(int argCount, Value* args);
        static Value ListSlice(int argCount, Value* args);
        static Value ListFlatten(int argCount, Value* args);
        static Value ListUnique(int argCount, Value* args);
        static Value ListFill(int argCount, Value* args);
        static Value ListRepeat(int argCount, Value* args);
        static Value ListPop(int argCount, Value* args);

        // ── Standard Library Phase 2: Map Extras ──
        static Value MapMerge(int argCount, Value* args);
        static Value MapEntries(int argCount, Value* args);

        // ── Standard Library Phase 2: Extended Math ──
        static Value MathStandardDeviation(int argCount, Value* args);
        static Value MathVariance(int argCount, Value* args);
        static Value MathMedian(int argCount, Value* args);
        static Value MathFactorial(int argCount, Value* args);
        static Value MathIsPrime(int argCount, Value* args);
        static Value MathGCD(int argCount, Value* args);
        static Value MathLCM(int argCount, Value* args);
        static Value MathDegToRad(int argCount, Value* args);
        static Value MathRadToDeg(int argCount, Value* args);
        static Value MathFmod(int argCount, Value* args);
        static Value MathHypot(int argCount, Value* args);
        static Value MathCbrt(int argCount, Value* args);

        // ── Standard Library Phase 2: Date / Time ──
        static Value DateNow(int argCount, Value* args);
        static Value DateGetYear(int argCount, Value* args);
        static Value DateGetMonth(int argCount, Value* args);
        static Value DateGetDay(int argCount, Value* args);
        static Value DateGetHours(int argCount, Value* args);
        static Value DateGetMinutes(int argCount, Value* args);
        static Value DateGetSeconds(int argCount, Value* args);
        static Value DateFormat(int argCount, Value* args);
        static Value DateGetDayOfWeek(int argCount, Value* args);
        static Value DateGetDayOfYear(int argCount, Value* args);
        static Value DateIsLeapYear(int argCount, Value* args);
        static Value DateDiffSeconds(int argCount, Value* args);

        // ── Standard Library Phase 2: Path Utilities ──
        static Value PathGetExtension(int argCount, Value* args);
        static Value PathGetFileName(int argCount, Value* args);
        static Value PathGetFileNameWithoutExtension(int argCount, Value* args);
        static Value PathGetDirectoryName(int argCount, Value* args);
        static Value PathCombine(int argCount, Value* args);
        static Value PathIsAbsolute(int argCount, Value* args);

        // ── Standard Library Phase 2: Color Utilities ──
        static Value ColorHexToRGB(int argCount, Value* args);
        static Value ColorRGBToHex(int argCount, Value* args);
        static Value ColorLerp(int argCount, Value* args);
        static Value ColorInvert(int argCount, Value* args);
        static Value ColorBrightness(int argCount, Value* args);

        // ── Standard Library Phase 2: Geometry ──
        static Value GeomDistance2D(int argCount, Value* args);
        static Value GeomCircleIntersect(int argCount, Value* args);
        static Value GeomAABBIntersect(int argCount, Value* args);
        static Value GeomPolygonArea(int argCount, Value* args);
        static Value GeomPointInRect(int argCount, Value* args);
        static Value GeomAngleBetween(int argCount, Value* args);

        // ── Standard Library Phase 2: Regex ──
        static Value RegexIsMatch(int argCount, Value* args);
        static Value RegexReplace(int argCount, Value* args);

        // ── Standard Library Phase 2: Environment ──
        static Value EnvGetOS(int argCount, Value* args);
        static Value EnvGetProcessorCount(int argCount, Value* args);

        // ── Standard Library Phase 2: Type Utilities ──
        static Value TypeOf(int argCount, Value* args);
        static Value ToStringNative(int argCount, Value* args);
        static Value ToNumberNative(int argCount, Value* args);
        static Value ParseInt(int argCount, Value* args);
        static Value ParseFloat(int argCount, Value* args);

        // ── Standard Library Phase 3: Vec3 Extras ──
        static Value Vec3Add(int argCount, Value* args);
        static Value Vec3Sub(int argCount, Value* args);
        static Value Vec3Mul(int argCount, Value* args);
        static Value Vec3Div(int argCount, Value* args);
        static Value Vec3Lerp(int argCount, Value* args);
        static Value Vec3Reflect(int argCount, Value* args);

        // ── Standard Library Phase 3: Matrix 4x4 ──
        static Value Mat4Identity(int argCount, Value* args);
        static Value Mat4Translate(int argCount, Value* args);
        static Value Mat4Rotate(int argCount, Value* args);
        static Value Mat4Scale(int argCount, Value* args);
        static Value Mat4Multiply(int argCount, Value* args);
        static Value Mat4Inverse(int argCount, Value* args);
        static Value Mat4Transpose(int argCount, Value* args);
        static Value Mat4LookAt(int argCount, Value* args);
        static Value Mat4Perspective(int argCount, Value* args);
        static Value Mat4Ortho(int argCount, Value* args);

        // ── Standard Library Phase 3: Bitwise ──
        static Value BitAnd(int argCount, Value* args);
        static Value BitOr(int argCount, Value* args);
        static Value BitXor(int argCount, Value* args);
        static Value BitNot(int argCount, Value* args);
        static Value BitShiftLeft(int argCount, Value* args);
        static Value BitShiftRight(int argCount, Value* args);

        // ── Standard Library Phase 3: Random Advanced ──
        static Value RandGaussian(int argCount, Value* args);
        static Value RandUUID(int argCount, Value* args);
        static Value RandColor(int argCount, Value* args);
        static Value RandInUnitSphere(int argCount, Value* args);
        static Value RandInUnitCircle(int argCount, Value* args);

        // ── Standard Library Phase 3: File IO ──
        static Value FileExists(int argCount, Value* args);
        static Value FileReadText(int argCount, Value* args);
        static Value FileWriteText(int argCount, Value* args);
        static Value FileDelete(int argCount, Value* args);
        static Value FileGetSize(int argCount, Value* args);

        // ── Standard Library Phase 3: URL ──
        static Value URLEncode(int argCount, Value* args);
        static Value URLDecode(int argCount, Value* args);

        // ── Standard Library Phase 4: Math Utilities ──
        static Value MathLog2(int argCount, Value* args);
        static Value MathLog1p(int argCount, Value* args);
        static Value MathExpm1(int argCount, Value* args);
        static Value MathAsinh(int argCount, Value* args);
        static Value MathAcosh(int argCount, Value* args);
        static Value MathAtanh(int argCount, Value* args);
        static Value MathClamp01(int argCount, Value* args);
        static Value MathSmoothStep(int argCount, Value* args);
        static Value MathPingPong(int argCount, Value* args);
        static Value MathRepeat(int argCount, Value* args);
        static Value MathSumOfSquares(int argCount, Value* args);
        static Value MathDegrees(int argCount, Value* args);
        static Value MathRadians(int argCount, Value* args);
        static Value MathNormalizeAngle(int argCount, Value* args);

        // ── Standard Library Phase 4: Statistical ──
        static Value StatMode(int argCount, Value* args);
        static Value StatRange(int argCount, Value* args);
        static Value StatMean(int argCount, Value* args);
        static Value StatGeometricMean(int argCount, Value* args);
        static Value StatHarmonicMean(int argCount, Value* args);
        static Value StatRootMeanSquare(int argCount, Value* args);

        // ── Standard Library Phase 4: Advanced Color ──
        static Value ColorRGBToHSV(int argCount, Value* args);
        static Value ColorHSVToRGB(int argCount, Value* args);
        static Value ColorRGBToHSL(int argCount, Value* args);
        static Value ColorHSLToRGB(int argCount, Value* args);
        static Value ColorToGrayscale(int argCount, Value* args);
        static Value ColorDarken(int argCount, Value* args);
        static Value ColorLighten(int argCount, Value* args);
        static Value ColorBlendMultiply(int argCount, Value* args);
        static Value ColorBlendScreen(int argCount, Value* args);
        static Value ColorBlendOverlay(int argCount, Value* args);
        static Value ColorDesaturate(int argCount, Value* args);
        static Value ColorSaturate(int argCount, Value* args);

        // ── Standard Library Phase 4: Geometry 3D ──
        static Value GeomDistance3D(int argCount, Value* args);
        static Value GeomSphereIntersect(int argCount, Value* args);
        static Value GeomAABBIntersect3D(int argCount, Value* args);
        static Value GeomPointInSphere(int argCount, Value* args);
        static Value GeomPointInAABB3D(int argCount, Value* args);
        static Value GeomTriangleArea3D(int argCount, Value* args);
        static Value GeomTriangleNormal(int argCount, Value* args);
        static Value GeomRaySphereIntersect(int argCount, Value* args);
        static Value GeomRayAABBIntersect(int argCount, Value* args);
        static Value GeomDistanceSquared3D(int argCount, Value* args);

        // ── Standard Library Phase 4: Cryptography & Hashing ──
        static Value HashSHA1Fallback(int argCount, Value* args);
        static Value HashSHA256Fallback(int argCount, Value* args);
        static Value HashCRC32(int argCount, Value* args);
        static Value HexEncode(int argCount, Value* args);
        static Value HexDecode(int argCount, Value* args);
        static Value ROT13(int argCount, Value* args);

        // ── Standard Library Phase 4: String Extras ──
        static Value StringLeft(int argCount, Value* args);
        static Value StringRight(int argCount, Value* args);
        static Value StringIsWhitespace(int argCount, Value* args);
        static Value StringIsLowerCase(int argCount, Value* args);
        static Value StringIsUpperCase(int argCount, Value* args);
        static Value StringRemovePrefix(int argCount, Value* args);
        static Value StringRemoveSuffix(int argCount, Value* args);
        static Value StringCountWords(int argCount, Value* args);
        static Value StringToTitleCase(int argCount, Value* args);
        static Value StringSwapCase(int argCount, Value* args);
        static Value StringIsAscii(int argCount, Value* args);
        static Value StringReverseWords(int argCount, Value* args);

        // ── Standard Library Phase 4: List Extras ──
        static Value ListChunk(int argCount, Value* args);
        static Value ListDifference(int argCount, Value* args);
        static Value ListIntersection(int argCount, Value* args);
        static Value ListUnion(int argCount, Value* args);
        static Value ListWithout(int argCount, Value* args);
        static Value ListCompact(int argCount, Value* args);
        static Value ListPad(int argCount, Value* args);
        static Value ListLastIndexOf(int argCount, Value* args);
        static Value ListDrop(int argCount, Value* args);
        static Value ListTake(int argCount, Value* args);
        static Value ListReverseInPlace(int argCount, Value* args);
        static Value ListRotate(int argCount, Value* args);

        // ── Standard Library Phase 4: Map Extras ──
        static Value MapHasValue(int argCount, Value* args);
        static Value MapInvert(int argCount, Value* args);
        static Value MapClone(int argCount, Value* args);
        static Value MapIsEmpty(int argCount, Value* args);
        static Value MapDefaultGet(int argCount, Value* args);

        // ── Standard Library Phase 4: URL & Path Extras ──
        static Value PathNormalize(int argCount, Value* args);
        static Value PathIsRelative(int argCount, Value* args);
        static Value URLGetHost(int argCount, Value* args);
        static Value URLGetProtocol(int argCount, Value* args);
        static Value URLGetPath(int argCount, Value* args);
        static Value URLGetQuery(int argCount, Value* args);

        // ── Quaternion Operations ──
        static Value QuatCreate(int argCount, Value* args);
        static Value QuatMultiply(int argCount, Value* args);
        static Value QuatNormalize(int argCount, Value* args);
        static Value QuatConjugate(int argCount, Value* args);
        static Value QuatInverse(int argCount, Value* args);
        static Value QuatFromAxisAngle(int argCount, Value* args);
        static Value QuatToEuler(int argCount, Value* args);
        static Value QuatFromEuler(int argCount, Value* args);
        static Value QuatSlerp(int argCount, Value* args);
        static Value QuatDot(int argCount, Value* args);
        static Value QuatLength(int argCount, Value* args);
        static Value QuatRotateVec3(int argCount, Value* args);

        // ── Easing Functions ──
        static Value EaseInQuad(int argCount, Value* args);
        static Value EaseOutQuad(int argCount, Value* args);
        static Value EaseInOutQuad(int argCount, Value* args);
        static Value EaseInCubic(int argCount, Value* args);
        static Value EaseOutCubic(int argCount, Value* args);
        static Value EaseInOutCubic(int argCount, Value* args);
        static Value EaseInSine(int argCount, Value* args);
        static Value EaseOutSine(int argCount, Value* args);
        static Value EaseInOutSine(int argCount, Value* args);
        static Value EaseInExpo(int argCount, Value* args);
        static Value EaseOutExpo(int argCount, Value* args);
        static Value EaseInOutExpo(int argCount, Value* args);
        static Value EaseInElastic(int argCount, Value* args);
        static Value EaseOutElastic(int argCount, Value* args);
        static Value EaseInBounce(int argCount, Value* args);
        static Value EaseOutBounce(int argCount, Value* args);

        // ── Noise & Procedural ──
        static Value NoiseHash(int argCount, Value* args);
        static Value NoiseFade(int argCount, Value* args);
        static Value NoiseGrad1D(int argCount, Value* args);
        static Value NoisePerlin1D(int argCount, Value* args);
        static Value NoiseValueNoise2D(int argCount, Value* args);
        static Value NoiseFBM1D(int argCount, Value* args);

        // ── Type Checking ──
        static Value IsInt(int argCount, Value* args);
        static Value IsFloat(int argCount, Value* args);
        static Value IsString(int argCount, Value* args);
        static Value IsBool(int argCount, Value* args);
        static Value IsNull(int argCount, Value* args);
        static Value IsList(int argCount, Value* args);
        static Value IsMap(int argCount, Value* args);
        static Value IsNumber(int argCount, Value* args);

        // ── Type Conversion ──
        static Value ToInt(int argCount, Value* args);
        static Value ToFloat(int argCount, Value* args);
        static Value ToString(int argCount, Value* args);
        static Value ToBool(int argCount, Value* args);
        static Value CharToCode(int argCount, Value* args);
        static Value CodeToChar(int argCount, Value* args);

        // ── Encoding ──
        static Value EncodeUTF8Length(int argCount, Value* args);

        // ── String Advanced ──
        static Value StringCenter(int argCount, Value* args);
        static Value StringLJust(int argCount, Value* args);
        static Value StringRJust(int argCount, Value* args);
        static Value StringExpandTabs(int argCount, Value* args);
        static Value StringZFill(int argCount, Value* args);
        static Value StringIsNumeric(int argCount, Value* args);
        static Value StringIsAlphaNumeric(int argCount, Value* args);
        static Value StringSlice(int argCount, Value* args);

        // ── List Functional ──
        static Value ListZip(int argCount, Value* args);
        static Value ListProduct(int argCount, Value* args);
        static Value ListCountValue(int argCount, Value* args);

        // ── Map Advanced ──
        static Value MapFromLists(int argCount, Value* args);
        static Value MapToList(int argCount, Value* args);
        static Value MapFilter(int argCount, Value* args);
        static Value MapCount(int argCount, Value* args);

        // ── Signal & Wave ──
        static Value WaveSine(int argCount, Value* args);
        static Value WaveSquare(int argCount, Value* args);
        static Value WaveSawtooth(int argCount, Value* args);
        static Value WaveTriangle(int argCount, Value* args);
        static Value WavePulse(int argCount, Value* args);

        // ── Physics Math ──
        static Value PhysMathGravityForce(int argCount, Value* args);
        static Value PhysMathKineticEnergy(int argCount, Value* args);
        static Value PhysMathProjectileRange(int argCount, Value* args);
        static Value PhysMathTerminalVelocity(int argCount, Value* args);
        static Value PhysMathSpringForce(int argCount, Value* args);
        static Value PhysMathMomentum(int argCount, Value* args);

        // ── System Info ──
        static Value SysUptime(int argCount, Value* args);
        static Value SysPID(int argCount, Value* args);
        static Value SysThreadID(int argCount, Value* args);

        // ── Standard Library Phase 6 Restored Declarations ──
        static Value CryptoMD5(int argCount, Value* args);
        static Value CryptoSHA1(int argCount, Value* args);
        static Value CryptoSHA256(int argCount, Value* args);
        static Value CryptoCRC32(int argCount, Value* args);
        static Value CryptoAdler32(int argCount, Value* args);
        static Value CryptoHashCombine(int argCount, Value* args);
        static Value TimeNow(int argCount, Value* args);
        static Value TimeFormatDate(int argCount, Value* args);
        static Value TimeIsLeapYear(int argCount, Value* args);
        static Value TimeDaysInMonth(int argCount, Value* args);
        static Value TimeAddDays(int argCount, Value* args);
        static Value TimeDiffSeconds(int argCount, Value* args);
        static Value Mat4Create(int argCount, Value* args);
        static Value Mat4Determinant(int argCount, Value* args);
        static Value Vec3Create(int argCount, Value* args);
        static Value Vec3Project(int argCount, Value* args);
        static Value Vec3Angle(int argCount, Value* args);
        static Value Vec2Create(int argCount, Value* args);
        static Value Vec2Add(int argCount, Value* args);
        static Value Vec2Sub(int argCount, Value* args);
        static Value Vec2Dot(int argCount, Value* args);
        static Value Vec2Distance(int argCount, Value* args);
        static Value Vec2Angle(int argCount, Value* args);
        static Value Vec2Normalize(int argCount, Value* args);
        static Value GeomPointInPolygon(int argCount, Value* args);
        static Value GeomLineIntersect2D(int argCount, Value* args);
        static Value GeomAABBIntersect2D(int argCount, Value* args);
        static Value GeomClosestPointOnLine(int argCount, Value* args);
        static Value PathGetDirectory(int argCount, Value* args);
        static Value PathChangeExtension(int argCount, Value* args);
        static Value FileExistsVirtual(int argCount, Value* args);
        static Value FileGetSizeVirtual(int argCount, Value* args);
        static Value RandomGaussian(int argCount, Value* args);
        static Value RandomRange(int argCount, Value* args);
        static Value RandomChoice(int argCount, Value* args);
        static Value RandomShuffleList(int argCount, Value* args);
        static Value RandomColor(int argCount, Value* args);
        static Value RandomUnitVec2(int argCount, Value* args);
        static Value RandomUnitVec3(int argCount, Value* args);
        static Value NoiseSimplex2D(int argCount, Value* args);
        static Value ColorBlend(int argCount, Value* args);
        static Value ColorGrayscale(int argCount, Value* args);
        static Value ColorLuminance(int argCount, Value* args);
        static Value BitCount(int argCount, Value* args);
        static Value BitToggle(int argCount, Value* args);
        static Value BitCheck(int argCount, Value* args);
        static Value PatternMatch(int argCount, Value* args);
        static Value PatternReplace(int argCount, Value* args);
        static Value PatternSearch(int argCount, Value* args);
        static Value PatternExtract(int argCount, Value* args);
        static Value StringTitle(int argCount, Value* args);

        // =========================================================================
        //  PHASE 7 EXTENSION DECLARATIONS (ADVANCED MATH, ALGORITHMS, QUATERNIONS)
        // =========================================================================
        
        // ── Math 3 (Advanced Interpolation) ──
        static Value MathRemap(int argCount, Value* args);
        static Value MathWrap(int argCount, Value* args);
        static Value MathBezierQuad(int argCount, Value* args);
        static Value MathBezierCubic(int argCount, Value* args);
        static Value MathCatmullRom(int argCount, Value* args);
        static Value MathHermite(int argCount, Value* args);

        // ── Encoding ──
        static Value Rot13(int argCount, Value* args);

        // ── Set Operations (Lists) ──
        static Value SetUnion(int argCount, Value* args);
        static Value SetIntersection(int argCount, Value* args);
        static Value SetDifference(int argCount, Value* args);
        static Value SetIsSubset(int argCount, Value* args);
        static Value SetIsSuperset(int argCount, Value* args);

        // ── Advanced List Operations ──
        static Value ListPushFront(int argCount, Value* args);
        static Value ListPopFront(int argCount, Value* args);
        static Value ListPopBack(int argCount, Value* args);
        static Value ListInsertAt(int argCount, Value* args);
        static Value ListSortAscending(int argCount, Value* args);
        static Value ListSortDescending(int argCount, Value* args);

        // ── Advanced String Processing ──
        static Value StringJoin(int argCount, Value* args);

        // ── Color Space Advanced ──
        static Value ColorToCMYK(int argCount, Value* args);
        static Value ColorFromCMYK(int argCount, Value* args);
        static Value ColorToHSL(int argCount, Value* args);
        static Value ColorFromHSL(int argCount, Value* args);

        // ── Binary/Hex Math ──
        static Value BinaryToDecimal(int argCount, Value* args);
        static Value DecimalToBinary(int argCount, Value* args);
        static Value HexToDecimal(int argCount, Value* args);
        static Value DecimalToHex(int argCount, Value* args);

        // ── Matrix 3x3 ──
        static Value Mat3Create(int argCount, Value* args);
        static Value Mat3Identity(int argCount, Value* args);
        static Value Mat3Determinant(int argCount, Value* args);
        static Value Mat3Transpose(int argCount, Value* args);
        static Value Mat3Multiply(int argCount, Value* args);

        // ── Quaternion Math ──
        static Value QuatIdentity(int argCount, Value* args);

        // =========================================================
        //  STANDARD LIBRARY PHASE 8: ADVANCED DATA, SIGNAL & AI
        // =========================================================
        
        // ── JSON Serialization ──
        static Value JSONStringify(int argCount, Value* args);
        static Value JSONParse(int argCount, Value* args);

        // ── Regex ──
        static Value RegexMatch(int argCount, Value* args);

        // ── Data Compression ──
        static Value CompressRLE(int argCount, Value* args);
        static Value DecompressRLE(int argCount, Value* args);
        static Value CompressLZ77(int argCount, Value* args);
        static Value DecompressLZ77(int argCount, Value* args);

        // ── Advanced Math & Number Theory ──
        static Value MathPrimeFactors(int argCount, Value* args);

        // ── Digital Signal Processing ──
        static Value DSPConvolve1D(int argCount, Value* args);

        // =========================================================
        //  STANDARD LIBRARY PHASE 9: NETWORKING, BINARY & ML
        // =========================================================
        
        // ── Networking (HTTP/WebSocket Mocks) ──
        static Value NetHttpRequest(int argCount, Value* args);
        static Value NetWebSocketConnect(int argCount, Value* args);
        static Value NetWebSocketSend(int argCount, Value* args);
        static Value NetWebSocketReceive(int argCount, Value* args);

        // ── Binary Buffer Manipulation ──
        static Value BufferCreate(int argCount, Value* args);
        static Value BufferWriteInt8(int argCount, Value* args);
        static Value BufferReadInt8(int argCount, Value* args);
        static Value BufferWriteInt32(int argCount, Value* args);
        static Value BufferReadInt32(int argCount, Value* args);
        static Value BufferWriteFloat(int argCount, Value* args);
        static Value BufferReadFloat(int argCount, Value* args);
        static Value BufferWriteString(int argCount, Value* args);
        static Value BufferReadString(int argCount, Value* args);
        static Value BufferToBase64(int argCount, Value* args);
        static Value BufferFromBase64(int argCount, Value* args);

        // ── Advanced ML / Analytics ──
        static Value MLKMeansCluster(int argCount, Value* args);
        static Value MLLinearRegression(int argCount, Value* args);
        static Value MLCalculateMean(int argCount, Value* args);
        static Value MLCalculateVariance(int argCount, Value* args);
        static Value MLCalculateStandardDeviation(int argCount, Value* args);
        
        // ── Advanced Geometry 2D ──
        static Value GeomBezierPoint2D(int argCount, Value* args);
        static Value GeomCatmullRomPoint2D(int argCount, Value* args);
        static Value GeomPolygonCentroid(int argCount, Value* args);

        // ── Advanced Geometry 3D ──
        static Value GeomBezierPoint3D(int argCount, Value* args);
        static Value GeomCatmullRomPoint3D(int argCount, Value* args);
        static Value GeomClosestPointOnTriangle3D(int argCount, Value* args);

        // ── Game State/Save System ──
        static Value StateSaveGame(int argCount, Value* args);
        static Value StateLoadGame(int argCount, Value* args);

        // =========================================================
        //  STANDARD LIBRARY PHASE 10: ALGORITHMIC CORE & IMAGE PROC
        // =========================================================

        // ── Image Processing API ──
        static Value ImageCreate(int argCount, Value* args);
        static Value ImageSetPixel(int argCount, Value* args);
        static Value ImageGetPixel(int argCount, Value* args);
        static Value ImageDrawLine(int argCount, Value* args);
        static Value ImageDrawCircle(int argCount, Value* args);
        static Value ImageBoxBlur(int argCount, Value* args);

        // ── Computational Geometry ──
        static Value GeomConvexHull2D(int argCount, Value* args);
        static Value GeomEarClipTriangulate(int argCount, Value* args);
        static Value GeomLineIntersection2D(int argCount, Value* args);

        // ── Math Expression Parser ──
        static Value MathEvalExpression(int argCount, Value* args);

        // ── Phase 11: Transform, PhysicsMath & Advanced Geometry ──
        static Value PhysMathExplicitEuler(int argCount, Value* args);
        static Value PhysMathSemiImplicitEuler(int argCount, Value* args);
        static Value PhysMathVelocityVerlet(int argCount, Value* args);
        static Value PhysMathRK4(int argCount, Value* args);
        static Value PhysMathComputeDragForce(int argCount, Value* args);
        static Value PhysMathComputeMagnusForce(int argCount, Value* args);
        static Value PhysMathComputeBuoyancyForce(int argCount, Value* args);
        static Value PhysMathResolveElasticCollision(int argCount, Value* args);
        static Value PhysMathSolveDistanceConstraint(int argCount, Value* args);

        static Value TransformGetLocalMatrix(int argCount, Value* args);
        static Value TransformHierarchyCalcGlobal(int argCount, Value* args);
        static Value TransformPoint(int argCount, Value* args);

        static Value GeomRayCircleIntersect2D(int argCount, Value* args);
        static Value GeomTriangleTriangleIntersect3D(int argCount, Value* args);
        
        static Value BSplineInterpolate(int argCount, Value* args);
        static Value HermiteTangent(int argCount, Value* args);
        // ── Complex Math ──
        static Value ComplexAdd(int argCount, Value* args);
        static Value ComplexExp(int argCount, Value* args);
        // ── Dual Quaternion ──
        static Value DualQuatFromTR(int argCount, Value* args);
        // ── Sampling ──
        static Value SamplePoissonDisk2D(int argCount, Value* args);
        // ── Color Space ──
        static Value RGBtoOKLAB(int argCount, Value* args);

        // ── Phase 12: Calculus & Advanced Math ──
        static Value LinearAlgebraDeterminant(int argCount, Value* args);
        static Value CompGeomPolygonArea3D(int argCount, Value* args);
        static Value SurfaceBezier(int argCount, Value* args);
        static Value SignalWindowHamming(int argCount, Value* args);

        // ── Phase 13: Advanced Subsystems (Kinematics, Topology, Stats, Num, Fractals) ──
        static Value MathSolveFABRIK(int argCount, Value* args);
        static Value MathTopologyEuler(int argCount, Value* args);
        static Value MathGradientDescent(int argCount, Value* args);
        static Value MathStatNormalPDF(int argCount, Value* args);
        static Value MathFractalMandelbrot(int argCount, Value* args);

        // ── Cryptography & Data Encoding ──
        static Value CryptoRC4(int argCount, Value* args);
        static Value CryptoBase32Encode(int argCount, Value* args);
        static Value CryptoBase32Decode(int argCount, Value* args);

        // ── Phase 14: New Math Modules ──
        static Value MathSHEvaluateLight(int argCount, Value* args);
        static Value MathSpatialCrossMotion(int argCount, Value* args);
        static Value MathDelaunayTriangulate(int argCount, Value* args);
        static Value MathHashMurmur3(int argCount, Value* args);
        static Value MathCurveNURBS(int argCount, Value* args);
        static Value MathSignalDCT(int argCount, Value* args);

        // ── Phase 15: Epic Expansion ──
        static Value MathTensorOuterProduct(int argCount, Value* args);
        static Value MathGraphDijkstra(int argCount, Value* args);
        static Value MathFluidSPHPoly6(int argCount, Value* args);
        static Value MathDiffGeomCurvature(int argCount, Value* args);
        static Value MathCellularConway(int argCount, Value* args);

        // ── Phase 16: God-Tier Math ──
        static Value MathFourierFFT(int argCount, Value* args);
        static Value MathSphericalHaversine(int argCount, Value* args);
        static Value MathGeometricAlgebraMultivectorMul(int argCount, Value* args);
        static Value MathMarkovNextState(int argCount, Value* args);
        static Value MathFuzzyTrapezoid(int argCount, Value* args);
        static Value MathVoronoiLloydRelax(int argCount, Value* args);
    };
}
