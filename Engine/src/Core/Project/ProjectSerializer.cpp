#include "ProjectSerializer.h"
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace Conqueror {

    ProjectSerializer::ProjectSerializer(Ref<Project> project)
        : m_Project(project)
    {
    }

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath)
    {
        const auto& config = m_Project->GetConfig();

        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Project" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << config.Name;
        out << YAML::Key << "Organization" << YAML::Value << config.Organization;
        out << YAML::Key << "Version" << YAML::Value << config.Version;
        out << YAML::Key << "TargetPlatform" << YAML::Value << config.TargetPlatform;
        out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
        out << YAML::Key << "AssetDirectory" << YAML::Value << config.AssetDirectory;
        out << YAML::Key << "ScriptModulePath" << YAML::Value << config.ScriptModulePath;
        out << YAML::EndMap;
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
        return true;
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath)
    {
        YAML::Node data;
        try
        {
            data = YAML::LoadFile(filepath.string());
        }
        catch (YAML::ParserException e)
        {
            return false;
        }

        auto projectNode = data["Project"];
        if (!projectNode || !projectNode.IsMap())
            return false;

        auto& config = m_Project->GetConfig();
        if (projectNode["Name"])
            config.Name = projectNode["Name"].as<std::string>();
        
        if (projectNode["Organization"])
            config.Organization = projectNode["Organization"].as<std::string>();
        if (projectNode["Version"])
            config.Version = projectNode["Version"].as<std::string>();
        if (projectNode["TargetPlatform"])
            config.TargetPlatform = projectNode["TargetPlatform"].as<std::string>();
        if (projectNode["StartScene"])
            config.StartScene = projectNode["StartScene"].as<std::string>();
        if (projectNode["AssetDirectory"])
            config.AssetDirectory = projectNode["AssetDirectory"].as<std::string>();
        if (projectNode["ScriptModulePath"])
            config.ScriptModulePath = projectNode["ScriptModulePath"].as<std::string>();

        // Set the root project directory to the directory containing this .cqproj file
        m_Project->SetProjectDirectory(filepath.parent_path());

        return true;
    }
}
