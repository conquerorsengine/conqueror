#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Launcher
{
    struct HubTemplate
    {
        std::string Id;
        std::string Name;
        std::string Category;
        std::string Description;
        std::string Icon;                    // Font Awesome glifi
        glm::vec4 ColorA{ 0.2f, 0.4f, 0.8f, 1.0f };
        glm::vec4 ColorB{ 0.1f, 0.2f, 0.4f, 1.0f };
        std::vector<std::string> Addons;     // önceden kurulacak katalog addon klasörleri
        std::vector<std::string> Features;   // kart üzerindeki maddeler
        std::string Industry = "Games";      // Games, Film / Video & Live Events, Architecture, Automotive, Simulation
    };

    const std::vector<HubTemplate>& GetTemplates();
    // Unreal'deki sektör listesi (Yeni Proje sayfasının sol sütunu)
    const std::vector<std::string>& GetIndustries();
    const HubTemplate* FindTemplate(const std::string& id);

    struct NewProjectOptions
    {
        std::string Name;
        std::filesystem::path Location;      // proje bu klasörün altında <Name> olarak oluşturulur
        std::string TemplateId = "basic3d";
        int PhysicsBackend = 0;              // 0 Jolt, 1 MuJoCo
        bool InstallAddons = true;
    };

    // Klasör yapısı, .cqproj, başlangıç sahnesi, betikler ve addon'lar
    bool CreateProject(const NewProjectOptions& options, std::filesystem::path& outProjectDir, std::string& error);
    bool IsValidProjectName(const std::string& name, std::string* why = nullptr);
    // Unity Hub gibi: ad o konumda boşsa adın kendisi, doluysa sıradaki ilk boş numara ("My Game (1)", "My Game (2)" ...).
    // Silinmiş projelerin numaraları yeniden kullanılır (40 proje olsa da (1) boşsa (1))
    std::string UniqueProjectName(const std::filesystem::path& location, const std::string& name);

    // HubTemplatesPro.cpp: sektör şablonları (Third Person, Top Down, Vehicle, Virtual Production, Archviz, Configurator, Crowd)
    bool IsProTemplate(const std::string& id);
    bool BuildProTemplate(const std::string& id, Scene& scene, const std::filesystem::path& dir, std::string& error);
}
