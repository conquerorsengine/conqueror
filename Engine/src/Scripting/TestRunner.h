#pragma once

#include "Core/Base/Base.h"

#include <string>
#include <vector>

// Test Script + Test Runner (Unity Test Framework karşılığı): TestScript'ten türeyen betiklerdeki Test* metotları
// Edit Mode'da, PlayTest* metotları Play Mode'da çalışır. Assert çağrıları sonucu yazar.
namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Scripting
{
    class NativeRegistry;
}

namespace Conqueror::Scripting::TestRunner
{
    enum class Status { NotRun, Running, Passed, Failed };

    struct Test
    {
        std::string Script;     // mutlak yol
        std::string Class;
        std::string Method;
        bool PlayMode = false;
        Status State = Status::NotRun;
        std::vector<std::string> Messages;
        double Seconds = 0.0;
    };

    CQ_API std::vector<Test>& Tests();
    CQ_API void Refresh();
    // Seçili ad boşsa hepsi
    CQ_API void RunEditMode(const std::string& onlyMethod = "");
    // Play Mode testlerini başlatmaya hazırla; EditorLayer Play'e geçer, sonra her kare Update çağrılır
    CQ_API void QueuePlayMode(const std::string& onlyMethod = "");
    CQ_API bool HasQueuedPlayMode();
    // true: bitti, Play durdurulabilir
    CQ_API bool UpdatePlayMode(Scene& scene, float dt);
    CQ_API void CancelPlayMode();
    CQ_API int CountWithStatus(Status status);
    // "Test Script" şablonu
    CQ_API std::string NewScriptTemplate(const std::string& className);
    void RegisterNatives(NativeRegistry* vm);
    // Editor Utility betiği: Run metodu düzenleyici sahnesinde çalışır
    CQ_API bool RunEditorUtility(const std::string& script, Scene* scene, std::string* error = nullptr);
    CQ_API bool IsEditorUtility(const std::string& script);
}
