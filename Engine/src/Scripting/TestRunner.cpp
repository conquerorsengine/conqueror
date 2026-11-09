#include "TestRunner.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/ScriptRuntime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace Conqueror::Scripting::TestRunner
{
    namespace fs = std::filesystem;

    namespace
    {
        std::vector<Test> s_Tests;
        Test* s_Current = nullptr;

        double Now()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        void Failure(const std::string& message)
        {
            if (s_Current)
            {
                s_Current->State = Status::Failed;
                s_Current->Messages.push_back(message);
            }
            else
                CQ_CORE_ERROR("[Test] {0}", message);
        }

        std::string Suffix(int argc, CQS::Value* args, int index)
        {
            return argc > index ? ": " + args[index].ToString() : std::string();
        }

        struct Live
        {
            ScriptRuntime* Runtime = nullptr;
            void* Instance = nullptr;
        };

        Live Create(Test& t, uint32_t entity)
        {
            Live live;
            live.Runtime = ScriptRuntimes::ForPath(t.Script);
            if (!live.Runtime || !live.Runtime->IsAvailable())
            {
                Failure("No runtime for this script language.");
                return live;
            }
            std::string error;
            if (!live.Runtime->LoadScript(t.Script, &error))
            {
                Failure("Script failed to compile: " + error);
                live.Runtime = nullptr;
                return live;
            }
            if (t.Class.empty())
                t.Class = live.Runtime->GetDefaultClass(t.Script);
            live.Instance = live.Runtime->CreateInstance(t.Script, t.Class, entity);
            if (!live.Instance)
                Failure("Could not create an instance of " + t.Class + ".");
            return live;
        }

        void Destroy(const Live& live)
        {
            if (live.Runtime && live.Instance)
                live.Runtime->DestroyInstance(live.Instance);
        }

        bool Call(const Live& live, const std::string& method, CQS::Value* result = nullptr)
        {
            if (!live.Runtime->HasMethod(live.Instance, method))
                return true;
            return live.Runtime->InvokeWithResult(live.Instance, method, nullptr, 0, result);
        }

        void Finish(Test& t, double start)
        {
            t.Seconds = Now() - start;
            if (t.State == Status::Running)
                t.State = Status::Passed;
        }

        struct PlayQueue
        {
            bool Queued = false;
            bool Active = false;
            std::string Only;
            size_t Index = 0;
            int WaitFrames = 3;
            float WaitSeconds = -1.0f;
            Live Instance;
            uint32_t Entity = 0;
            double Start = 0.0;
        } s_Play;
    }

    std::vector<Test>& Tests()
    {
        return s_Tests;
    }

    void Refresh()
    {
        std::vector<Test> found;
        const fs::path assets = Project::GetAssetDirectory();
        std::error_code ec;
        if (assets.empty() || !fs::exists(assets, ec))
        {
            s_Tests.clear();
            return;
        }
        static const std::regex base(R"((:\s*(public\s+)?|extends\s+|\()TestScript\b)");
        static const std::regex method(R"(\b(func|def|function|void|fn)\s+((Test|test_|PlayTest)\w*)\s*\()");
        for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
        {
            if (ec)
                break;
            if (!it->is_regular_file(ec) || !ScriptRuntimes::IsScriptFile(it->path().string()))
                continue;
            const std::string script = it->path().string();
            std::ifstream in(script, std::ios::binary);
            std::string line, text;
            bool isTest = false;
            while (std::getline(in, line))
            {
                const auto first = line.find_first_not_of(" \t");
                if (first == std::string::npos || line.compare(first, 2, "//") == 0 || line[first] == '#')
                    continue;
                isTest = isTest || std::regex_search(line, base);
                text += line + "\n";
            }
            if (!isTest)
                continue;
            auto* runtime = ScriptRuntimes::ForPath(script);
            std::string className = fs::path(script).stem().string();
            if (runtime)
            {
                std::vector<std::string> classes;
                if (runtime->GetClasses(script, classes, nullptr) && !classes.empty())
                    className = classes.front();
            }
            for (std::sregex_iterator m(text.begin(), text.end(), method), done; m != done; ++m)
            {
                Test t;
                t.Script = script;
                t.Class = className;
                t.Method = (*m)[2].str();
                if (t.Method.size() > 7 && t.Method.compare(t.Method.size() - 7, 7, "_Verify") == 0)
                    continue;
                t.PlayMode = t.Method.rfind("PlayTest", 0) == 0;
                // Önceki sonuç korunur
                for (const auto& old : s_Tests)
                    if (old.Script == t.Script && old.Method == t.Method)
                    {
                        t.State = old.State;
                        t.Messages = old.Messages;
                        t.Seconds = old.Seconds;
                    }
                found.push_back(std::move(t));
            }
        }
        std::sort(found.begin(), found.end(), [](const Test& a, const Test& b) { return std::tie(a.Script, a.Method) < std::tie(b.Script, b.Method); });
        s_Tests = std::move(found);
    }

    void RunEditMode(const std::string& onlyMethod)
    {
        Refresh();
        ScriptRuntimes::PlayStart();
        for (auto& t : s_Tests)
        {
            if (t.PlayMode || (!onlyMethod.empty() && t.Method != onlyMethod))
                continue;
            t.State = Status::Running;
            t.Messages.clear();
            s_Current = &t;
            const double start = Now();
            {
                ScriptContext::EntityScope scope(ScriptContext::NullEntity);
                Live live = Create(t, ScriptContext::NullEntity);
                if (live.Instance)
                {
                    Call(live, "SetUp");
                    if (!Call(live, t.Method))
                        Failure("The script raised an error (see Console).");
                    Call(live, "TearDown");
                    Destroy(live);
                }
            }
            s_Current = nullptr;
            Finish(t, start);
        }
        ScriptRuntimes::PlayStop();
    }

    void QueuePlayMode(const std::string& onlyMethod)
    {
        Refresh();
        for (auto& t : s_Tests)
            if (t.PlayMode && (onlyMethod.empty() || t.Method == onlyMethod))
            {
                t.State = Status::Running;
                t.Messages.clear();
            }
        s_Play = PlayQueue{};
        s_Play.Queued = true;
        s_Play.Only = onlyMethod;
    }

    bool HasQueuedPlayMode()
    {
        return s_Play.Queued;
    }

    void CancelPlayMode()
    {
        if (s_Play.Instance.Instance)
            Destroy(s_Play.Instance);
        for (auto& t : s_Tests)
            if (t.State == Status::Running)
            {
                t.State = Status::Failed;
                t.Messages.push_back("Play Mode stopped before this test finished.");
            }
        s_Play = PlayQueue{};
        s_Current = nullptr;
    }

    bool UpdatePlayMode(Scene& scene, float dt)
    {
        if (!s_Play.Queued)
            return false;
        // Sahne ilk karelerini oynasın
        if (s_Play.WaitFrames > 0)
        {
            --s_Play.WaitFrames;
            return false;
        }
        ScriptContext::SetActiveScene(&scene);
        // Bekleyen doğrulama
        if (s_Play.Active)
        {
            if (s_Play.WaitSeconds > 0.0f)
            {
                s_Play.WaitSeconds -= dt;
                return false;
            }
            Test& t = s_Tests[s_Play.Index];
            s_Current = &t;
            ScriptContext::EntityScope scope(s_Play.Entity);
            if (!Call(s_Play.Instance, t.Method + "_Verify"))
                Failure("The script raised an error (see Console).");
            Call(s_Play.Instance, "TearDown");
            Destroy(s_Play.Instance);
            s_Play.Instance = {};
            s_Current = nullptr;
            Finish(t, s_Play.Start);
            s_Play.Active = false;
            ++s_Play.Index;
        }
        for (; s_Play.Index < s_Tests.size(); ++s_Play.Index)
        {
            Test& t = s_Tests[s_Play.Index];
            if (!t.PlayMode || (!s_Play.Only.empty() && t.Method != s_Play.Only))
                continue;
            s_Current = &t;
            s_Play.Start = Now();
            Entity host = scene.CreateEntity("[Test Runner] " + t.Method);
            s_Play.Entity = (uint32_t)(entt::entity)host;
            ScriptContext::EntityScope scope(s_Play.Entity);
            s_Play.Instance = Create(t, s_Play.Entity);
            if (!s_Play.Instance.Instance)
            {
                s_Current = nullptr;
                Finish(t, s_Play.Start);
                continue;
            }
            Call(s_Play.Instance, "SetUp");
            CQS::Value result = CQS::Value::MakeNull();
            if (!Call(s_Play.Instance, t.Method, &result))
                Failure("The script raised an error (see Console).");
            s_Current = nullptr;
            // Sayı döndürürse o kadar saniye sonra <Metot>_Verify çağrılır
            if (result.IsNumber() && result.ToNumber() > 0.0)
            {
                s_Play.Active = true;
                s_Play.WaitSeconds = (float)result.ToNumber();
                return false;
            }
            Call(s_Play.Instance, "TearDown");
            Destroy(s_Play.Instance);
            s_Play.Instance = {};
            Finish(t, s_Play.Start);
        }
        s_Play.Queued = false;
        return true;
    }

    int CountWithStatus(Status status)
    {
        int n = 0;
        for (const auto& t : s_Tests)
            n += t.State == status ? 1 : 0;
        return n;
    }

    std::string NewScriptTemplate(const std::string& className)
    {
        return "using nativebridge;\n\n"
               "// Run from Window > Test Runner.\n"
               "// Test* methods run in Edit Mode, PlayTest* methods in Play Mode.\n"
               "// A PlayTest may return a number: its <name>_Verify method then runs that many seconds later.\n"
               "script " + className + " : TestScript\n{\n"
               "    func SetUp() { }\n"
               "    func TearDown() { }\n\n"
               "    func TestAddition()\n    {\n        AssertEqual(1 + 1, 2, \"one plus one\");\n    }\n"
               "}\n";
    }

    bool IsEditorUtility(const std::string& script)
    {
        std::ifstream in(script, std::ios::binary);
        std::string line;
        while (std::getline(in, line))
            if (line.find(": EditorUtility") != std::string::npos || line.find(":EditorUtility") != std::string::npos)
                return true;
        return false;
    }

    bool RunEditorUtility(const std::string& script, Scene* scene, std::string* error)
    {
        auto* runtime = ScriptRuntimes::ForPath(script);
        if (!runtime || !runtime->IsAvailable())
        {
            if (error) *error = "No runtime for this script.";
            return false;
        }
        if (!runtime->LoadScript(script, error))
            return false;
        Scene* previous = ScriptContext::GetActiveScene();
        ScriptContext::SetActiveScene(scene);
        bool ok = false;
        {
            ScriptContext::EntityScope scope(ScriptContext::NullEntity);
            void* instance = runtime->CreateInstance(script, runtime->GetDefaultClass(script), ScriptContext::NullEntity);
            if (instance)
            {
                ok = !runtime->HasMethod(instance, "Run") || runtime->Invoke(instance, "Run", nullptr, 0);
                runtime->DestroyInstance(instance);
            }
        }
        ScriptContext::SetActiveScene(previous);
        if (!ok && error && error->empty())
            *error = "The script raised an error (see Console).";
        return ok;
    }

    void RegisterNatives(NativeRegistry* vm)
    {
        using CQS::Value;
        vm->DefineNative("AssertTrue", [](int argc, Value* args) -> Value {
            if (argc < 1 || !args[0].IsTruthy())
                Failure("Expected true" + Suffix(argc, args, 1));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("AssertFalse", [](int argc, Value* args) -> Value {
            if (argc < 1 || args[0].IsTruthy())
                Failure("Expected false" + Suffix(argc, args, 1));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("AssertEqual", [](int argc, Value* args) -> Value {
            if (argc < 2)
                return Value::MakeNull();
            const bool same = args[0].IsNumber() && args[1].IsNumber() ? args[0].ToNumber() == args[1].ToNumber() : args[0].ToString() == args[1].ToString();
            if (!same)
                Failure("Expected " + args[1].ToString() + " but got " + args[0].ToString() + Suffix(argc, args, 2));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("AssertNotEqual", [](int argc, Value* args) -> Value {
            if (argc >= 2 && args[0].ToString() == args[1].ToString())
                Failure("Expected a value other than " + args[1].ToString() + Suffix(argc, args, 2));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("AssertNear", [](int argc, Value* args) -> Value {
            if (argc < 3)
                return Value::MakeNull();
            if (std::fabs(args[0].ToNumber() - args[1].ToNumber()) > args[2].ToNumber())
                Failure("Expected " + args[1].ToString() + " within " + args[2].ToString() + " but got " + args[0].ToString() + Suffix(argc, args, 3));
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("AssertFail", [](int argc, Value* args) -> Value {
            Failure(argc > 0 ? args[0].ToString() : std::string("Test failed."));
            return Value::MakeNull();
        }, -1);
    }
}
