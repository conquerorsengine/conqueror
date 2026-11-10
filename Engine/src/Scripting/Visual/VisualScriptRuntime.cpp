#include "Scripting/ScriptRuntime.h"
#include "Scripting/Visual/VisualGraph.h"
#include "Scripting/ConquerorScript/CQSEngine.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

// Görsel betikler (.cqgraph): grafik ConquerorScript'e çevrilir ve CQS çalışma zamanında (JIT) çalışır.
// Üretilen kod Library/ScriptAssemblies/visual altında saklanır; derleme hataları düğümlere eşlenir.

namespace Conqueror::Scripting
{
    namespace
    {
        namespace fs = std::filesystem;

        // "Line 12:5 ..." -> 12
        int ExtractLine(const std::string& msg)
        {
            size_t p = msg.find("Line ");
            return p == std::string::npos ? 0 : std::atoi(msg.c_str() + p + 5);
        }

        std::string StripLocation(const std::string& msg)
        {
            // "File.cqs: Line 12:5 at 'x': mesaj" -> "mesaj"
            size_t p = msg.find("Line ");
            if (p == std::string::npos) return msg;
            size_t colon = msg.find(": ", p);
            std::string rest = colon == std::string::npos ? msg : msg.substr(colon + 2);
            size_t q = rest.find("': ");
            if ((rest.rfind("at '", 0) == 0 || rest.rfind("after '", 0) == 0) && q != std::string::npos)
                rest = rest.substr(q + 3);
            return rest;
        }
    }

    class VisualScriptRuntime final : public ScriptRuntime
    {
    public:
        const char* GetName() const override { return "Visual Script"; }
        const char* GetLanguageId() const override { return "graph"; }
        std::vector<std::string> GetExtensions() const override { return { ".cqgraph" }; }

        void EndFrame() override {}

        bool LoadScript(const std::string& path, std::string* error) override
        {
            std::string generated;
            if (!Generate(path, generated, error))
                return false;
            std::string cqsError;
            if (!CQS::CQSEngine::LoadScript(generated, &cqsError))
            {
                std::string mapped = MapErrors(path, cqsError);
                CQ_CORE_ERROR("[Visual Script] {0}", mapped);
                if (error) *error = mapped;
                return false;
            }
            return true;
        }

        std::string GetDefaultClass(const std::string& path) override
        {
            return Visual::SanitizeIdentifier(fs::path(path).stem().string());
        }

        void* CreateInstance(const std::string& path, const std::string& className, uint32_t entity) override
        {
            (void)path;
            return CQS::CQSEngine::Instantiate(className, entity);
        }

        void DestroyInstance(void* instance) override { CQS::CQSEngine::DestroyInstance(instance); }
        bool HasMethod(void* instance, const std::string& method) override { return CQS::CQSEngine::HasMethod(instance, method); }

        bool Invoke(void* instance, const std::string& method, const CQS::Value* args, int argc) override
        {
            return CQS::CQSEngine::InvokeMethod(instance, method, args, argc);
        }

        bool SetField(void* instance, const std::string& field, const ScriptValue& value) override
        {
            return CQS::CQSEngine::SetField(instance, Visual::SanitizeIdentifier(field), ScriptRuntimes::ToValue(value));
        }

        bool GetField(void* instance, const std::string& field, ScriptValue& value, std::string* display) override
        {
            CQS::Value v;
            if (!CQS::CQSEngine::GetField(instance, Visual::SanitizeIdentifier(field), v))
                return false;
            value = ScriptRuntimes::FromValue(v);
            if (display) *display = v.ToString();
            return true;
        }

        int HotReload(const std::string& path, std::string* error) override
        {
            std::string generated;
            if (!Generate(path, generated, error))
                return -1;
            return CQS::CQSEngine::HotReload(generated, error);
        }

        bool GetClasses(const std::string& path, std::vector<std::string>& classes, std::string* error) override
        {
            (void)error;
            classes.push_back(GetDefaultClass(path));
            return true;
        }

        bool GetFields(const std::string& path, const std::string& className, std::vector<ScriptFieldInfo>& fields, std::string* error) override
        {
            (void)className;
            Visual::Graph graph;
            if (!graph.Load(path, error))
                return false;
            for (auto& v : graph.Variables)
            {
                if (!v.Public) continue;
                ScriptFieldInfo info;
                info.Name = Visual::SanitizeIdentifier(v.Name);
                info.HasDefault = true;
                switch (v.Type)
                {
                    case Visual::PinType::Number:
                        info.TypeName = "float";
                        info.Default = ScriptValue::MakeFloat(std::atof(v.Default.c_str()));
                        break;
                    case Visual::PinType::Bool:
                        info.TypeName = "bool";
                        info.Default = ScriptValue::MakeBool(v.Default == "true" || v.Default == "1");
                        break;
                    case Visual::PinType::String:
                        info.TypeName = "string";
                        info.Default = ScriptValue::MakeString(v.Default);
                        break;
                    default:
                        info.TypeName = Visual::PinTypeName(v.Type);
                        info.HasDefault = false;
                        break;
                }
                if (!v.Tooltip.empty())
                    info.Attributes.push_back("Tooltip(\"" + v.Tooltip + "\")");
                fields.push_back(info);
            }
            return true;
        }

        bool CheckSyntax(const std::string& source, const std::string& name, std::vector<std::string>& errors, std::vector<std::string>* warnings) override
        {
            Visual::Graph graph;
            std::string err;
            if (!graph.FromJson(source, &err))
            {
                errors.push_back(name + ": Line 1: " + err);
                return false;
            }
            std::vector<Visual::CompileMessage> messages;
            std::vector<int> lineToNode;
            std::string code = graph.Transpile(GetDefaultClass(name), messages, &lineToNode);
            for (auto& m : messages)
            {
                std::string text = name + ": " + (m.NodeId ? "Node " + std::to_string(m.NodeId) + ": " : "") + m.Text;
                if (m.IsWarning) { if (warnings) warnings->push_back(text); }
                else errors.push_back(text);
            }
            std::vector<std::string> cqsErrors, cqsWarnings;
            CQS::CQSEngine::CheckSyntax(code, name + ".cqs", cqsErrors, &cqsWarnings);
            for (auto& e : cqsErrors) errors.push_back(name + ": " + NodePrefix(e, lineToNode) + StripLocation(e));
            if (warnings)
                for (auto& w : cqsWarnings) warnings->push_back(name + ": " + NodePrefix(w, lineToNode) + StripLocation(w));
            return errors.empty();
        }

        std::string GetNewScriptTemplate(const std::string& className) const override
        {
            Visual::Graph graph;
            graph.Name = className;
            int start = graph.AddNode("event.start", 40, 40);
            int print = graph.AddNode("action.print", 300, 40);
            graph.FindNode(print)->InputDefaults["Message"] = className + " started";
            graph.Connect(start, "Then", print, "In");
            graph.AddNode("event.update", 40, 220);
            Visual::Variable speed;
            speed.Name = "speed";
            speed.Type = Visual::PinType::Number;
            speed.Default = "1.0";
            graph.Variables.push_back(speed);
            return graph.ToJson();
        }

        // Üretilen kod yolu (hata ayıklama ve derleme)
        static fs::path GeneratedPath(const std::string& graphPath)
        {
            fs::path projectDir = Project::GetActiveProjectDirectory();
            std::error_code ec;
            fs::path dir = (projectDir.empty() ? fs::temp_directory_path(ec) / "ConquerorEngine" : projectDir / "Library") / "ScriptAssemblies" / "visual";
            fs::create_directories(dir, ec);
            return dir / (Visual::SanitizeIdentifier(fs::path(graphPath).stem().string()) + ".cqs");
        }

    private:
        std::unordered_map<std::string, std::vector<int>> m_LineMaps;

        bool Generate(const std::string& path, std::string& generatedPath, std::string* error)
        {
            Visual::Graph graph;
            if (!graph.Load(path, error))
            {
                CQ_CORE_ERROR("[Visual Script] Cannot load {0}", path);
                return false;
            }
            std::vector<Visual::CompileMessage> messages;
            std::vector<int> lineToNode;
            // Editörde düğüm izleri (Blueprint hata ayıklayıcısı: vurgu, isabet sayısı, kesme noktası)
            const bool trace = Visual::Debugger::IsEnabled();
            if (trace)
                Visual::Debugger::RegisterGraph(GetDefaultClass(path), path);
            std::string code = graph.Transpile(GetDefaultClass(path), messages, &lineToNode, trace);
            bool ok = true;
            for (auto& m : messages)
            {
                std::string text = fs::path(path).filename().string() + (m.NodeId ? " (node " + std::to_string(m.NodeId) + ")" : "") + ": " + m.Text;
                if (m.IsWarning) CQ_CORE_WARN("[Visual Script] {0}", text);
                else
                {
                    CQ_CORE_ERROR("[Visual Script] {0}", text);
                    if (ok && error) *error = text;
                    ok = false;
                }
            }
            if (!ok)
                return false;
            fs::path out = GeneratedPath(path);
            std::string existing;
            {
                std::ifstream f(out, std::ios::binary);
                std::stringstream ss;
                ss << f.rdbuf();
                existing = ss.str();
            }
            if (existing != code)
                std::ofstream(out, std::ios::binary) << code;
            generatedPath = out.string();
            m_LineMaps[path] = lineToNode;
            return true;
        }

        std::string MapErrors(const std::string& path, const std::string& errors)
        {
            auto it = m_LineMaps.find(path);
            std::stringstream ss(errors);
            std::string line, out;
            while (std::getline(ss, line))
            {
                std::string prefix = it != m_LineMaps.end() ? NodePrefix(line, it->second) : "";
                out += (out.empty() ? "" : "\n") + fs::path(path).filename().string() + ": " + prefix + StripLocation(line);
            }
            return out;
        }

        static std::string NodePrefix(const std::string& msg, const std::vector<int>& lineToNode)
        {
            int line = ExtractLine(msg);
            if (line > 0 && line <= (int)lineToNode.size() && lineToNode[(size_t)line - 1] > 0)
                return "Node " + std::to_string(lineToNode[(size_t)line - 1]) + ": ";
            return "";
        }
    };

    std::unique_ptr<ScriptRuntime> CreateVisualScriptRuntime()
    {
        return std::make_unique<VisualScriptRuntime>();
    }
}
