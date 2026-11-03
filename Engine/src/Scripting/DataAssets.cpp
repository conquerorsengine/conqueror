#include "DataAssets.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scripting/ScriptRuntime.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace Conqueror::DataAssets
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string Resolve(const std::string& path)
        {
            return path.empty() ? path : SceneSerializer::ResolveAssetPath(path);
        }

        std::string ReadText(const std::string& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return {};
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        fs::file_time_type TimeOf(const std::string& path)
        {
            std::error_code ec;
            const auto t = fs::last_write_time(path, ec);
            return ec ? fs::file_time_type{} : t;
        }

        void EmitValue(YAML::Emitter& out, const Scripting::ScriptValue& v)
        {
            using K = Scripting::ScriptValue::Kind;
            out << YAML::BeginMap;
            switch (v.Type)
            {
            case K::Bool: out << YAML::Key << "Bool" << YAML::Value << v.Bool; break;
            case K::Int: out << YAML::Key << "Int" << YAML::Value << v.Int; break;
            case K::Float: out << YAML::Key << "Float" << YAML::Value << v.Float; break;
            case K::String: out << YAML::Key << "String" << YAML::Value << v.String; break;
            default: out << YAML::Key << "Null" << YAML::Value << true; break;
            }
            out << YAML::EndMap;
        }

        Scripting::ScriptValue ReadValue(const YAML::Node& n)
        {
            if (n["Bool"]) return Scripting::ScriptValue::MakeBool(n["Bool"].as<bool>());
            if (n["Int"]) return Scripting::ScriptValue::MakeInt(n["Int"].as<int64_t>());
            if (n["Float"]) return Scripting::ScriptValue::MakeFloat(n["Float"].as<double>());
            if (n["String"]) return Scripting::ScriptValue::MakeString(n["String"].as<std::string>());
            return {};
        }

        // Alan tanımları önbelleği (betik değişince yenilenir)
        struct FieldCache
        {
            fs::file_time_type Time{};
            std::vector<Scripting::ScriptFieldInfo> Fields;
            std::string Error;
        };
        std::unordered_map<std::string, FieldCache> s_Fields;
    }

    bool Load(const std::string& pathIn, DataAssetFile& out)
    {
        out = {};
        const std::string path = Resolve(pathIn);
        try
        {
            YAML::Node root = YAML::LoadFile(path);
            YAML::Node d = root["DataAsset"];
            if (!d)
                return false;
            if (d["Script"]) out.Script = d["Script"].as<std::string>();
            if (d["Class"]) out.Class = d["Class"].as<std::string>();
            if (auto fields = d["Fields"]; fields && fields.IsMap())
                for (auto it = fields.begin(); it != fields.end(); ++it)
                    out.Fields[it->first.as<std::string>()] = ReadValue(it->second);
            return true;
        }
        catch (const std::exception& e)
        {
            CQ_CORE_WARN("Data Asset: cannot read {0}: {1}", path, e.what());
            return false;
        }
    }

    bool Save(const std::string& pathIn, const DataAssetFile& data)
    {
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "DataAsset" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Script" << YAML::Value << data.Script;
        out << YAML::Key << "Class" << YAML::Value << data.Class;
        out << YAML::Key << "Fields" << YAML::Value << YAML::BeginMap;
        for (const auto& [name, v] : data.Fields)
        {
            out << YAML::Key << name << YAML::Value;
            EmitValue(out, v);
        }
        out << YAML::EndMap << YAML::EndMap << YAML::EndMap;
        std::ofstream f(Resolve(pathIn), std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f << "# Conqueror's Engine - Data Asset\n" << out.c_str() << "\n";
        return true;
    }

    bool ClassFields(const std::string& scriptPathIn, const std::string& className, std::vector<Scripting::ScriptFieldInfo>& out, std::string* error)
    {
        const std::string scriptPath = Resolve(scriptPathIn);
        const std::string key = scriptPath + "|" + className;
        const auto time = TimeOf(scriptPath);
        auto it = s_Fields.find(key);
        if (it == s_Fields.end() || it->second.Time != time)
        {
            FieldCache c;
            c.Time = time;
            auto* runtime = IsCppHeader(scriptPath) ? Scripting::ScriptRuntimes::ForLanguage("cpp") : Scripting::ScriptRuntimes::ForPath(scriptPath);
            if (!runtime)
                c.Error = "No script language for " + scriptPath;
            else if (!runtime->IsAvailable())
                c.Error = std::string(runtime->GetName()) + " is not available: " + runtime->GetUnavailableReason();
            else if (!runtime->GetFields(scriptPath, className, c.Fields, &c.Error) && c.Error.empty())
                c.Error = "Cannot read the fields of " + className;
            it = s_Fields.insert_or_assign(key, std::move(c)).first;
        }
        out = it->second.Fields;
        if (error)
            *error = it->second.Error;
        return it->second.Error.empty();
    }

    bool Resolve(const std::string& path, std::vector<std::pair<std::string, Scripting::ScriptValue>>& out, std::string* error)
    {
        out.clear();
        DataAssetFile file;
        if (!Load(path, file))
        {
            if (error) *error = "Cannot read " + path;
            return false;
        }
        std::vector<Scripting::ScriptFieldInfo> fields;
        std::string ferr;
        ClassFields(file.Script, file.Class, fields, &ferr);
        for (const auto& f : fields)
        {
            auto it = file.Fields.find(f.Name);
            out.push_back({ f.Name, it != file.Fields.end() ? it->second : f.Default });
        }
        // Sınıfta artık olmayan ama dosyada kalan değerler de okunabilir kalsın
        for (const auto& [name, v] : file.Fields)
        {
            bool known = false;
            for (const auto& [n, _] : out)
                known |= n == name;
            if (!known)
                out.push_back({ name, v });
        }
        if (error) *error = ferr;
        return true;
    }

    bool IsCppHeader(const std::string& path)
    {
        std::string ext = fs::path(path).extension().string();
        for (char& c : ext)
            c = (char)std::tolower((unsigned char)c);
        return ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx";
    }

    bool IsDataScriptFile(const std::string& path)
    {
        return Scripting::ScriptRuntimes::IsScriptFile(path) || IsCppHeader(path);
    }

    namespace
    {
        // Dosyanın sözdizimi ailesi: "py" (# yorum, class X(Base)), "java", "cpp" (C/C++ kaynak ve başlık), "cs", "cqs"
        std::string LanguageOf(const std::string& path)
        {
            if (IsCppHeader(path))
                return "cpp";
            if (auto* runtime = Scripting::ScriptRuntimes::ForPath(path))
                return runtime->GetLanguageId();
            return {};
        }

        // Yorumları at (yorum satırındaki "class X : DataAsset" sınıf sayılmasın); dizgeler korunur
        std::string StripComments(const std::string& text, bool hashComments)
        {
            std::string out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (c == '"' || c == '\'')
                {
                    const char q = c;
                    out += c;
                    for (++i; i < text.size(); ++i)
                    {
                        out += text[i];
                        if (text[i] == '\\' && i + 1 < text.size())
                            out += text[++i];
                        else if (text[i] == q || text[i] == '\n')
                            break;
                    }
                    continue;
                }
                if (hashComments ? c == '#' : (c == '/' && i + 1 < text.size() && text[i + 1] == '/'))
                {
                    while (i < text.size() && text[i] != '\n')
                        ++i;
                    out += '\n';
                    continue;
                }
                if (!hashComments && c == '/' && i + 1 < text.size() && text[i + 1] == '*')
                {
                    i += 2;
                    while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
                    {
                        if (text[i] == '\n')
                            out += '\n';
                        ++i;
                    }
                    ++i;
                    continue;
                }
                out += c;
            }
            return out;
        }

        struct DataClassCache
        {
            fs::file_time_type Time{};
            std::vector<std::string> Classes;
        };
        std::unordered_map<std::string, DataClassCache> s_DataClasses;
    }

    std::vector<std::string> DataClassesInText(const std::string& textIn, const std::string& languageId)
    {
        std::vector<std::string> out;
        if (textIn.find(BaseClass) == std::string::npos)
            return out;
        const bool python = languageId == "py";
        const std::string text = StripComments(textIn, python);
        // Taban sınıf ad alanıyla da yazılabilir: cq::DataAsset, Conqueror.DataAsset, conqueror.DataAsset, nativebridge.DataAsset
        // CQS "script X : DataAsset", C# "class X : DataAsset", C++ "class X final : public cq::DataAsset", LuaQ "class X : DataAsset"
        static const std::regex colon("\\b(?:script|class|struct)\\s+([A-Za-z_]\\w*)\\s*(?:final\\s*)?:\\s*(?:(?:public|private|protected|virtual)\\s+)*" +
                                      std::string("(?:[A-Za-z_]\\w*(?:::|\\.))*DataAsset\\b"));
        // Python "class X(DataAsset)" / "class X(Mixin, DataAsset)"
        static const std::regex paren("\\bclass\\s+([A-Za-z_]\\w*)\\s*\\(([^)]*)\\)");
        // Java "class X extends DataAsset"
        static const std::regex extends("\\bclass\\s+([A-Za-z_]\\w*)(?:\\s*<[^>]*>)?\\s+extends\\s+(?:[A-Za-z_]\\w*\\.)*DataAsset\\b");
        auto add = [&](const std::string& n) {
            if (std::find(out.begin(), out.end(), n) == out.end())
                out.push_back(n);
        };
        if (languageId.empty() || (languageId != "py" && languageId != "java"))
            for (auto it = std::sregex_iterator(text.begin(), text.end(), colon); it != std::sregex_iterator(); ++it)
                add((*it)[1].str());
        if (languageId.empty() || python)
        {
            static const std::regex baseInList("(?:^|,)\\s*(?:[A-Za-z_]\\w*\\.)*DataAsset\\s*(?:,|$)");
            for (auto it = std::sregex_iterator(text.begin(), text.end(), paren); it != std::sregex_iterator(); ++it)
                if (std::regex_search((*it)[2].str(), baseInList))
                    add((*it)[1].str());
        }
        if (languageId.empty() || languageId == "java")
            for (auto it = std::sregex_iterator(text.begin(), text.end(), extends); it != std::sregex_iterator(); ++it)
                add((*it)[1].str());
        return out;
    }

    std::vector<std::string> DataClassesOf(const std::string& scriptPathIn)
    {
        const std::string file = Resolve(scriptPathIn);
        const auto time = TimeOf(file);
        auto it = s_DataClasses.find(file);
        if (it == s_DataClasses.end() || it->second.Time != time)
        {
            DataClassCache c;
            c.Time = time;
            c.Classes = DataClassesInText(ReadText(file), LanguageOf(file));
            it = s_DataClasses.insert_or_assign(file, std::move(c)).first;
        }
        return it->second.Classes;
    }

    bool IsDataOnlyScript(const std::string& scriptPathIn)
    {
        const std::string file = Resolve(scriptPathIn);
        const auto data = DataClassesOf(file);
        if (data.empty())
            return false;
        auto isData = [&](const std::string& n) { return std::find(data.begin(), data.end(), n) != data.end(); };
        const std::string lang = LanguageOf(file);
        const std::string stem = fs::path(file).stem().string();
        // C#/Java/C++: çalışma zamanı dosya adıyla aynı sınıfı seçer
        if ((lang == "cs" || lang == "java" || lang == "cpp") && isData(stem))
            return true;
        // Diğerleri (ve dosya adıyla sınıf yoksa): dosyadaki bütün sınıflar veri sınıfıysa
        const std::string text = StripComments(ReadText(file), lang == "py");
        static const std::regex decl("\\b(?:script|class|struct)\\s+([A-Za-z_]\\w*)");
        bool stemDeclared = false;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), decl); it != std::sregex_iterator(); ++it)
        {
            const std::string n = (*it)[1].str();
            stemDeclared |= n == stem;
            if (!isData(n) && (lang == "cqs" || lang == "py" || lang.empty()))
                return false;
        }
        if (lang == "cs" || lang == "java" || lang == "cpp")
        {
            if (stemDeclared)
                return false;   // dosya adıyla aynı davranış sınıfı var
            for (auto it = std::sregex_iterator(text.begin(), text.end(), decl); it != std::sregex_iterator(); ++it)
                if (!isData((*it)[1].str()))
                    return false;
        }
        return true;
    }

    bool IsDataClass(const std::string& scriptPathIn, const std::string& className)
    {
        if (className.empty())
            return false;
        const auto classes = DataClassesOf(scriptPathIn);
        return std::find(classes.begin(), classes.end(), className) != classes.end();
    }

    std::vector<std::pair<std::string, std::string>> FindDataClasses(const std::string& projectDirectory)
    {
        std::vector<std::pair<std::string, std::string>> out;
        std::error_code ec;
        const fs::path assets = fs::path(projectDirectory) / "Assets";
        if (!fs::is_directory(assets, ec))
            return out;
        for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec || !it->is_regular_file(ec))
                continue;
            const std::string file = it->path().string();
            if (!IsDataScriptFile(file))
                continue;
            for (const auto& cls : DataClassesOf(file))
                out.push_back({ fs::relative(it->path(), projectDirectory, ec).generic_string(), cls });
        }
        return out;
    }

    bool SupportsDataClasses(const std::string& languageId)
    {
        return languageId == "cqs" || languageId == "cs" || languageId == "java" || languageId == "py" || languageId == "cpp";
    }

    std::string NewScriptTemplate(const std::string& className, const std::string& languageId)
    {
        const std::string n = className;
        if (languageId == "cs")
            return "using Conqueror;\n"
                   "using static Conqueror.NativeBridge;\n\n"
                   "// Data Asset sınıfı (Unity ScriptableObject): public alanları veri şablonudur.\n"
                   "// Content Browser > Create > Data Asset > " + n + " ile bu sınıftan dosyalar oluşturun;\n"
                   "// oyunda: var data = DataAsset.Load<" + n + ">(\"Assets/New " + n + ".cqdata\"); data.value\n"
                   "public class " + n + " : DataAsset\n{\n"
                   "    [Tooltip(\"Shown in the Inspector of every " + n + " asset\")]\n"
                   "    public float value = 1f;\n"
                   "    public string displayName = \"\";\n"
                   "}\n";
        if (languageId == "java")
            return "import conqueror.*;\n"
                   "import static conqueror.NativeBridge.*;\n\n"
                   "// Data Asset sınıfı (Unity ScriptableObject): public alanları veri şablonudur.\n"
                   "// Content Browser > Create > Data Asset > " + n + " ile bu sınıftan dosyalar oluşturun;\n"
                   "// oyunda: " + n + " data = DataAsset.load(" + n + ".class, \"Assets/New " + n + ".cqdata\"); data.value\n"
                   "public class " + n + " extends DataAsset {\n"
                   "    @Tooltip(\"Shown in the Inspector of every " + n + " asset\")\n"
                   "    public float value = 1f;\n"
                   "    public String displayName = \"\";\n"
                   "}\n";
        if (languageId == "py")
            return "from nativebridge import *\n\n\n"
                   "# Data Asset sınıfı (Unity ScriptableObject): sınıf öznitelikleri veri şablonudur ('_' ile başlayanlar gizli).\n"
                   "# Content Browser > Create > Data Asset > " + n + " ile bu sınıftan dosyalar oluşturun;\n"
                   "# oyunda: data = " + n + ".load(\"Assets/New " + n + ".cqdata\"); data.value\n"
                   "class " + n + "(DataAsset):\n"
                   "    value = 1.0\n"
                   "    displayName = \"\"\n";
        if (languageId == "cpp")
            return "#pragma once\n"
                   "#include <Conqueror/Script.h>\n"
                   "#include \"NativeBridge.g.h\"\n\n"
                   "// Data Asset sınıfı (Unreal UDataAsset / Unity ScriptableObject): CQ_FIELD alanları veri şablonudur.\n"
                   "// Content Browser > Create > Data Asset > " + n + " ile bu sınıftan dosyalar oluşturun. Sınıf başlıktadır:\n"
                   "// betiklerde #include \"" + n + ".h\" ve auto data = cq::LoadDataAsset<" + n + ">(\"Assets/New " + n + ".cqdata\"); data.value\n"
                   "class " + n + " : public cq::DataAsset\n{\n"
                   "    CQ_SCRIPT_CLASS(" + n + ")\n"
                   "public:\n"
                   "    CQ_FIELD(float, value, 1.0f)\n"
                   "    CQ_FIELD(std::string, displayName, \"\")\n"
                   "};\n"
                   "CQ_SCRIPT(" + n + ")\n";
        return "using nativebridge;\n\n"
               "// Data Asset sınıfı (Unity ScriptableObject): public alanları veri şablonudur.\n"
               "// Content Browser > Create > Data Asset > " + n + " ile bu sınıftan dosyalar oluşturun;\n"
               "// oyunda: var data = LoadDataAsset(\"Assets/New " + n + ".cqdata\"); data.value\n"
               "script " + n + " : DataAsset\n{\n"
               "    [Tooltip(\"Shown in the Inspector of every " + n + " asset\")]\n"
               "    public float value = 1.0;\n"
               "    public string displayName = \"\";\n"
               "}\n";
    }
}
