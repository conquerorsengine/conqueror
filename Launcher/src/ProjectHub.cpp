#include "ProjectHub.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

#include <imgui.h>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <nfd.h>

#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Conqueror::Editor
{
    ProjectHub::ProjectHub()
    {
        CQ_CORE_INFO("ProjectHub: Constructor started");
        
        m_RecentProjectsFile = std::filesystem::current_path() / "recent_projects.yaml";
        CQ_CORE_INFO("ProjectHub: Recent projects file: {0}", m_RecentProjectsFile.string());
        
        // Default project path - home directory
        const char* homeDir = std::getenv("HOME");
        if (!homeDir)
            homeDir = std::getenv("USERPROFILE"); // Windows fallback
            
        if (homeDir)
        {
            std::filesystem::path projectsPath = std::filesystem::path(homeDir) / "ConquerorProjects";
            strncpy(m_ProjectPathBuffer, projectsPath.string().c_str(), sizeof(m_ProjectPathBuffer) - 1);
            m_ProjectPathBuffer[sizeof(m_ProjectPathBuffer) - 1] = '\0';
            CQ_CORE_INFO("ProjectHub: Default project path: {0}", m_ProjectPathBuffer);
        }
        
        // Load recent projects AFTER initializing buffers
        try
        {
            CQ_CORE_INFO("ProjectHub: Loading recent projects...");
            LoadRecentProjects();
            CQ_CORE_INFO("ProjectHub: Loaded {0} recent projects", m_RecentProjects.size());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to load recent projects in constructor: {0}", e.what());
            m_RecentProjects.clear();
        }
        
        CQ_CORE_INFO("ProjectHub: Constructor completed");

        // Logo yükle (Resources dizini exe yanına kopyalanıyor)
        std::filesystem::path logoPath = std::filesystem::current_path() / "Resources" / "Logo" / "Conqueror_Logo.png";
        if (std::filesystem::exists(logoPath))
        {
            int w, h, channels;
            unsigned char* data = stbi_load(logoPath.string().c_str(), &w, &h, &channels, 4);
            if (data)
            {
                glGenTextures(1, &m_LogoTexture);
                glBindTexture(GL_TEXTURE_2D, m_LogoTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
                CQ_CORE_INFO("ProjectHub: Logo loaded ({0}x{1})", w, h);
            }
        }
        else
        {
            CQ_CORE_WARN("ProjectHub: Logo not found at {0}", logoPath.string());
        }

        // 3D template resmi yükle
        std::filesystem::path template3DPath = std::filesystem::current_path() / "Resources" / "EngineImages" / "3d.png";
        if (std::filesystem::exists(template3DPath))
        {
            int w, h, channels;
            unsigned char* data = stbi_load(template3DPath.string().c_str(), &w, &h, &channels, 4);
            if (data)
            {
                glGenTextures(1, &m_3DTemplateTexture);
                glBindTexture(GL_TEXTURE_2D, m_3DTemplateTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            }
        }

        // 2D template resmi yükle
        std::filesystem::path template2DPath = std::filesystem::current_path() / "Resources" / "EngineImages" / "2d.png";
        if (std::filesystem::exists(template2DPath))
        {
            int w, h, channels;
            unsigned char* data = stbi_load(template2DPath.string().c_str(), &w, &h, &channels, 4);
            if (data)
            {
                glGenTextures(1, &m_2DTemplateTexture);
                glBindTexture(GL_TEXTURE_2D, m_2DTemplateTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            }
        }

        // Empty template resmi yükle
        std::filesystem::path templateEmptyPath = std::filesystem::current_path() / "Resources" / "EngineImages" / "empty.png";
        if (std::filesystem::exists(templateEmptyPath))
        {
            int w, h, channels;
            unsigned char* data = stbi_load(templateEmptyPath.string().c_str(), &w, &h, &channels, 4);
            if (data)
            {
                glGenTextures(1, &m_EmptyTemplateTexture);
                glBindTexture(GL_TEXTURE_2D, m_EmptyTemplateTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            }
        }
    }

    void ProjectHub::OnImGuiRender()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::Begin("ProjectHub", nullptr, window_flags);
        ImGui::PopStyleVar(4);

        // Main content (sidebar kaldırıldı)
        ImGui::BeginChild("MainContent", ImVec2(0, 0), false);
        {
            if (m_CurrentView == HubView::Projects)
            {
                if (m_ShowNewProject)
                {
                    ImGui::SetCursorPos(ImVec2(50, 40));
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                    ImGui::SetWindowFontScale(1.5f);
                    if (ImGui::Button("< Back", ImVec2(120, 50)))
                    {
                        m_ShowNewProject = false;
                    }
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(48);
                    ImGui::Text("New project");
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::PopStyleColor(2);
                    
                    ImGui::SetCursorPos(ImVec2(50, 110));
                    RenderNewProjectPanel();
                }
                else
                {
                    RenderProjectsHome();
                }
            }
            else if (m_CurrentView == HubView::Settings)
            {
                RenderSettings();
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void ProjectHub::RenderNewProjectPanel()
    {
        float contentWidth = ImGui::GetContentRegionAvail().x - 100;
        
        // Templates section (büyük + gri)
        ImGui::SetWindowFontScale(1.3f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::Text("Templates");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Template cards (daha büyük)
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::BeginChild("TemplateCards", ImVec2(contentWidth * 0.65f, 450), true);
        {
            ImGui::Spacing();
            
            // Template grid
            float cardWidth = 200.0f;
            float cardHeight = 140.0f;
            float spacing = 15.0f;
            
            ImGui::SetCursorPosX(spacing);
            
            // Empty Template Card
            ImGui::BeginGroup();
            {
                ImVec2 cursorPos = ImGui::GetCursorPos();
                bool isSelected = (m_SelectedTemplate == 0);
                
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, isSelected ? ImVec4(0.3f, 0.5f, 0.8f, 0.4f) : ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.8f, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.5f, 0.8f, 0.5f));
                
                if (ImGui::Button("##Empty", ImVec2(cardWidth, cardHeight)))
                {
                    m_SelectedTemplate = 0;
                }
                
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();
                
                if (m_EmptyTemplateTexture)
                {
                    ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + 10));
                    ImGui::Image((ImTextureID)(intptr_t)m_EmptyTemplateTexture, ImVec2(cardWidth - 20, cardHeight - 55));
                }
                
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + cardHeight - 35));
                ImGui::Text("Empty");
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + cardHeight - 20));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("Basic structure");
                ImGui::PopStyleColor();
            }
            ImGui::EndGroup();
            
            ImGui::SameLine(0, spacing);
            
            // 2D Template Card
            ImGui::BeginGroup();
            {
                ImVec2 cursorPos = ImGui::GetCursorPos();
                bool isSelected = (m_SelectedTemplate == 1);
                
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, isSelected ? ImVec4(0.3f, 0.5f, 0.8f, 0.4f) : ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.8f, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.5f, 0.8f, 0.5f));
                
                if (ImGui::Button("##2D", ImVec2(cardWidth, cardHeight)))
                {
                    m_SelectedTemplate = 1;
                }
                
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();
                
                if (m_2DTemplateTexture)
                {
                    ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + 10));
                    ImGui::Image((ImTextureID)(intptr_t)m_2DTemplateTexture, ImVec2(cardWidth - 20, cardHeight - 55));
                }
                
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + cardHeight - 35));
                ImGui::Text("2D Template");
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + cardHeight - 20));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("2D scene setup");
                ImGui::PopStyleColor();
            }
            ImGui::EndGroup();
            
            ImGui::SameLine(0, spacing);
            
            // 3D Template Card
            ImGui::BeginGroup();
            {
                ImVec2 cursorPos = ImGui::GetCursorPos();
                bool isSelected = (m_SelectedTemplate == 2);
                
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, isSelected ? ImVec4(0.3f, 0.5f, 0.8f, 0.4f) : ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.8f, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.5f, 0.8f, 0.5f));
                
                if (ImGui::Button("##3D", ImVec2(cardWidth, cardHeight)))
                {
                    m_SelectedTemplate = 2;
                }
                
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();
                
                if (m_3DTemplateTexture)
                {
                    ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + 10));
                    ImGui::Image((ImTextureID)(intptr_t)m_3DTemplateTexture, ImVec2(cardWidth - 20, cardHeight - 55));
                }
                
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + cardHeight - 35));
                ImGui::Text("3D Template");
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 10, cursorPos.y + cardHeight - 20));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("3D scene setup");
                ImGui::PopStyleColor();
            }
            ImGui::EndGroup();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        
        ImGui::SameLine();
        
        // Right panel - Recent projects
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        ImGui::BeginChild("RecentPanel", ImVec2(0, 400), true);
        {
            ImGui::Text("Recent Projects");
            ImGui::Separator();
            ImGui::Spacing();
            
            if (m_RecentProjects.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("No recent projects.\nCreate a new project to get started!");
                ImGui::PopStyleColor();
            }
            else
            {
                for (size_t i = 0; i < m_RecentProjects.size() && i < 5; i++)
                {
                    const auto& project = m_RecentProjects[i];
                    
                    ImGui::PushID((int)i);
                    
                    if (ImGui::Selectable(project.Name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 30)))
                    {
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            OpenProject(project);
                        }
                    }
                    
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Project settings
        ImGui::Text("Project name");
        ImGui::Spacing();
        ImGui::PushItemWidth(contentWidth * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::InputText("##ProjectName", m_ProjectNameBuffer, sizeof(m_ProjectNameBuffer));
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        ImGui::Spacing();
        
        ImGui::Text("Location");
        ImGui::Spacing();
        ImGui::PushItemWidth(contentWidth * 0.5f - 90);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::InputText("##ProjectPath", m_ProjectPathBuffer, sizeof(m_ProjectPathBuffer));
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();
        ImGui::SameLine();
        
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("Browse", ImVec2(80, 0)))
        {
            nfdchar_t* outPath = nullptr;
            nfdresult_t result = NFD_PickFolderU8(&outPath, nullptr);
            
            if (result == NFD_OKAY)
            {
                strncpy(m_ProjectPathBuffer, outPath, sizeof(m_ProjectPathBuffer) - 1);
                m_ProjectPathBuffer[sizeof(m_ProjectPathBuffer) - 1] = '\0';
                NFD_FreePathU8(outPath);
            }
        }
        ImGui::PopStyleVar();
        
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Create button
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.8f, 1.0f));
        
        if (ImGui::Button("  Create project", ImVec2(170, 40)))
        {
            std::filesystem::path projectPath = std::filesystem::path(m_ProjectPathBuffer) / m_ProjectNameBuffer;
            CreateNewProject(m_ProjectNameBuffer, projectPath, (ProjectTemplate)m_SelectedTemplate);
        }
        
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    void ProjectHub::RenderRecentProjectsPanel()
    {
        // Bu fonksiyon artık kullanılmıyor, yeni UI'da entegre edildi
    }

    void ProjectHub::CreateNewProject(const std::string& name, const std::filesystem::path& path, ProjectTemplate templateType)
    {
        try
        {
            CQ_CORE_INFO("[CreateNewProject] Starting: name={0}, path={1}, template={2}", name, path.string(), (int)templateType);

            // Create project directory
            if (!std::filesystem::exists(path))
            {
                std::filesystem::create_directories(path);
                CQ_CORE_INFO("[CreateNewProject] Created directory: {0}", path.string());
            }
            else
            {
                CQ_CORE_WARN("[CreateNewProject] Directory already exists: {0}", path.string());
            }

            // Create basic folder structure
            std::filesystem::create_directories(path / "Assets");
            std::filesystem::create_directories(path / "Assets" / "Scenes");
            std::filesystem::create_directories(path / "Assets" / "Scripts");
            std::filesystem::create_directories(path / "Assets" / "Textures");
            std::filesystem::create_directories(path / "Assets" / "Materials");
            std::filesystem::create_directories(path / "Assets" / "Models");
            std::filesystem::create_directories(path / "Assets" / "Audio");
            std::filesystem::create_directories(path / "Packages");
            CQ_CORE_INFO("[CreateNewProject] Folder structure created");

            // Setup template
            CQ_CORE_INFO("[CreateNewProject] Calling SetupProjectTemplate...");
            SetupProjectTemplate(path, templateType);
            CQ_CORE_INFO("[CreateNewProject] SetupProjectTemplate done");

            // Template kopyaladıysa cqproj dosyasını yeniden adlandır
            if (templateType != ProjectTemplate::Empty)
            {
                CQ_CORE_INFO("[CreateNewProject] Looking for .cqproj files in: {0}", path.string());
                bool foundCqproj = false;
                for (auto& entry : std::filesystem::directory_iterator(path))
                {
                    CQ_CORE_INFO("[CreateNewProject] Found file: {0}", entry.path().string());
                    if (entry.path().extension() == ".cqproj")
                    {
                        foundCqproj = true;
                        std::filesystem::path newPath = path / (name + ".cqproj");
                        CQ_CORE_INFO("[CreateNewProject] Renaming {0} -> {1}", entry.path().string(), newPath.string());
                        std::filesystem::rename(entry.path(), newPath);

                        if (!std::filesystem::exists(newPath))
                        {
                            CQ_CORE_ERROR("[CreateNewProject] RENAME FAILED - file does not exist after rename!");
                            continue;
                        }

                        // cqproj'yi oku, Name'i değiştir, düzgün yaz
                        CQ_CORE_INFO("[CreateNewProject] Loading YAML from: {0}", newPath.string());
                        YAML::Node proj = YAML::LoadFile(newPath.string());
                        if (proj["Project"])
                        {
                            proj["Project"]["Name"] = name;
                            CQ_CORE_INFO("[CreateNewProject] Updated Name to: {0}", name);
                        }
                        else
                        {
                            CQ_CORE_WARN("[CreateNewProject] No 'Project' key in YAML!");
                        }

                        YAML::Emitter out;
                        out << proj;
                        CQ_CORE_INFO("[CreateNewProject] Writing YAML to: {0}", newPath.string());
                        std::ofstream fout(newPath);
                        fout << out.c_str();
                        fout.flush();
                        fout.close();

                        if (std::filesystem::exists(newPath))
                        {
                            auto fileSize = std::filesystem::file_size(newPath);
                            CQ_CORE_INFO("[CreateNewProject] File written successfully, size={0} bytes", fileSize);
                        }
                        else
                        {
                            CQ_CORE_ERROR("[CreateNewProject] File does not exist after write!");
                        }
                        break;
                    }
                }
                if (!foundCqproj)
                {
                    CQ_CORE_ERROR("[CreateNewProject] No .cqproj file found after template copy!");
                }
            }
            else
            {
                CQ_CORE_INFO("[CreateNewProject] Empty template - creating cqproj from scratch");
                YAML::Emitter out;
                out << YAML::BeginMap;
                out << YAML::Key << "Project" << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << name;
                out << YAML::Key << "Organization" << YAML::Value << "";
                out << YAML::Key << "Version" << YAML::Value << "1.0.0";
                out << YAML::Key << "TargetPlatform" << YAML::Value << "Linux";
                out << YAML::Key << "StartScene" << YAML::Value << "Assets/Scenes/Scene.cqscene";
                out << YAML::Key << "AssetDirectory" << YAML::Value << "Assets";
                out << YAML::Key << "ScriptModulePath" << YAML::Value << "";
                out << YAML::EndMap;
                out << YAML::EndMap;

                std::ofstream fout(path / (name + ".cqproj"));
                fout << out.c_str();
                fout.flush();
                fout.close();
                CQ_CORE_INFO("[CreateNewProject] Empty cqproj created: {0}", (path / (name + ".cqproj")).string());
            }

            CQ_CORE_INFO("[CreateNewProject] Calling OpenProject...");
            ProjectInfo projectInfo(name, path);
            OpenProject(projectInfo);
            CQ_CORE_INFO("[CreateNewProject] Done");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("[CreateNewProject] FAILED: {0}", e.what());
        }
    }

    void ProjectHub::OpenProject(const ProjectInfo& project)
    {
        CQ_CORE_INFO("[OpenProject] Opening: name={0}, path={1}", project.Name, project.Path.string());

        if (!std::filesystem::exists(project.Path))
        {
            CQ_CORE_ERROR("[OpenProject] Project path does NOT exist: {0}", project.Path.string());
            return;
        }

        m_SelectedProject = project;
        m_ProjectSelected = true;
        
        AddToRecentProjects(project);
        
        if (m_ProjectOpenCallback)
            m_ProjectOpenCallback(project);

        std::string enginePath = std::filesystem::current_path().string() + "/ConquerorEngine";
        std::string projectPath = project.Path.string();
        
        CQ_CORE_INFO("[OpenProject] Engine path: {0}", enginePath);
        CQ_CORE_INFO("[OpenProject] Engine exists: {0}", std::filesystem::exists(enginePath));
        
        if (std::filesystem::exists(enginePath))
        {
            m_LaunchCommand = "\"" + enginePath + "\" \"" + projectPath + "\"";
            CQ_CORE_INFO("[OpenProject] Launch command: {0}", m_LaunchCommand);
        }
        else
        {
            CQ_CORE_ERROR("[OpenProject] ConquerorEngine NOT found at: {0}", enginePath);
        }
        
        m_ShouldClose = true;
        CQ_CORE_INFO("[OpenProject] m_ShouldClose set to true");
    }

    void ProjectHub::LoadRecentProjects()
    {
        m_RecentProjects.clear();
        m_RecentProjects.reserve(10); // Max 10 proje
        
        if (!std::filesystem::exists(m_RecentProjectsFile))
            return;

        try
        {
            std::ifstream file(m_RecentProjectsFile);
            if (!file.is_open())
                return;
                
            YAML::Node data = YAML::Load(file);
            file.close();
            
            if (!data || !data["RecentProjects"])
                return;
            
            const YAML::Node& projects = data["RecentProjects"];
            if (!projects.IsSequence())
                return;
                
            for (size_t i = 0; i < projects.size() && i < 10; i++)
            {
                const YAML::Node& projectNode = projects[i];
                
                if (!projectNode["Name"] || !projectNode["Path"])
                    continue;
                    
                ProjectInfo project;
                project.Name = projectNode["Name"].as<std::string>();
                project.Path = projectNode["Path"].as<std::string>();
                
                if (projectNode["LastOpened"])
                    project.LastOpened = projectNode["LastOpened"].as<std::string>();
                else
                    project.LastOpened = "Unknown";
                
                if (std::filesystem::exists(project.Path))
                    m_RecentProjects.push_back(project);
            }
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to load recent projects: {0}", e.what());
            m_RecentProjects.clear();
        }
    }

    void ProjectHub::SaveRecentProjects()
    {
        try
        {
            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "RecentProjects" << YAML::Value << YAML::BeginSeq;
            
            for (const auto& project : m_RecentProjects)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << project.Name;
                out << YAML::Key << "Path" << YAML::Value << project.Path.string();
                out << YAML::Key << "LastOpened" << YAML::Value << project.LastOpened;
                out << YAML::EndMap;
            }
            
            out << YAML::EndSeq;
            out << YAML::EndMap;

            std::ofstream fout(m_RecentProjectsFile);
            fout << out.c_str();
            fout.close();
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to save recent projects: {0}", e.what());
        }
    }

    void ProjectHub::AddToRecentProjects(const ProjectInfo& project)
    {
        // Referansı erişmeden önce kopyala (erase dangling reference yapar)
        ProjectInfo newProject = project;
        newProject.LastOpened = "Recently";

        // Remove if already exists
        auto it = std::find_if(m_RecentProjects.begin(), m_RecentProjects.end(),
            [&](const ProjectInfo& p) { return p.Path == newProject.Path; });
        
        if (it != m_RecentProjects.end())
            m_RecentProjects.erase(it);

        // Add to front
        m_RecentProjects.insert(m_RecentProjects.begin(), newProject);

        // Keep only last 10 projects
        if (m_RecentProjects.size() > 10)
            m_RecentProjects.resize(10);

        SaveRecentProjects();
    }

    void ProjectHub::SetupProjectTemplate(const std::filesystem::path& projectPath, ProjectTemplate templateType)
    {
        switch (templateType)
        {
            case ProjectTemplate::Empty:
                CreateEmptyTemplate(projectPath);
                break;
            case ProjectTemplate::Template2D:
                Create2DTemplate(projectPath);
                break;
            case ProjectTemplate::Template3D:
                Create3DTemplate(projectPath);
                break;
        }
    }

    void ProjectHub::CreateEmptyTemplate(const std::filesystem::path& projectPath)
    {
        // Boş scene oluştur
        auto scene = std::make_shared<Scene>();
        
        SceneSerializer serializer(scene);
        serializer.Serialize(projectPath / "Assets" / "Scenes" / "Scene.cqscene");
    }

    void ProjectHub::Create2DTemplate(const std::filesystem::path& projectPath)
    {
        std::filesystem::path templateDir = std::filesystem::current_path() / "Resources" / "DefaultProjects" / "2d";
        
        CQ_CORE_INFO("[Create2DTemplate] Template dir: {0}", templateDir.string());
        CQ_CORE_INFO("[Create2DTemplate] Template exists: {0}", std::filesystem::exists(templateDir));
        CQ_CORE_INFO("[Create2DTemplate] Target path: {0}", projectPath.string());
        
        if (std::filesystem::exists(templateDir))
        {
            CQ_CORE_INFO("[Create2DTemplate] Copying template...");
            std::filesystem::copy(templateDir, projectPath, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
            CQ_CORE_INFO("[Create2DTemplate] Copy done");
            
            for (auto& entry : std::filesystem::directory_iterator(projectPath))
            {
                CQ_CORE_INFO("[Create2DTemplate] Copied: {0}", entry.path().string());
            }
        }
        else
        {
            CQ_CORE_WARN("[Create2DTemplate] Template not found, creating empty scene");
            auto scene = std::make_shared<Scene>();
            SceneSerializer serializer(scene);
            serializer.Serialize(projectPath / "Assets" / "Scenes" / "Scene.cqscene");
        }
    }

    void ProjectHub::Create3DTemplate(const std::filesystem::path& projectPath)
    {
        std::filesystem::path templateDir = std::filesystem::current_path() / "Resources" / "DefaultProjects" / "3d";
        
        CQ_CORE_INFO("[Create3DTemplate] Template dir: {0}", templateDir.string());
        CQ_CORE_INFO("[Create3DTemplate] Template exists: {0}", std::filesystem::exists(templateDir));
        CQ_CORE_INFO("[Create3DTemplate] Target path: {0}", projectPath.string());
        
        if (std::filesystem::exists(templateDir))
        {
            CQ_CORE_INFO("[Create3DTemplate] Copying template...");
            std::filesystem::copy(templateDir, projectPath, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
            CQ_CORE_INFO("[Create3DTemplate] Copy done");
            
            for (auto& entry : std::filesystem::directory_iterator(projectPath))
            {
                CQ_CORE_INFO("[Create3DTemplate] Copied: {0}", entry.path().string());
            }
        }
        else
        {
            CQ_CORE_WARN("[Create3DTemplate] Template not found, creating empty scene");
            auto scene = std::make_shared<Scene>();
            SceneSerializer serializer(scene);
            serializer.Serialize(projectPath / "Assets" / "Scenes" / "Scene.cqscene");
        }
    }
}

namespace Conqueror::Editor
{
    void ProjectHub::RenderProjectsHome()
    {
        ImGui::SetCursorPos(ImVec2(50, 30));
        
        // Conqueror's Engine logosu
        if (m_LogoTexture)
        {
            ImGui::Image((ImTextureID)(intptr_t)m_LogoTexture, ImVec2(370, 130));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            ImGui::SetWindowFontScale(2.0f);
            ImGui::Text("Conqueror's Engine");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        }
        
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Projects başlığı + New Project butonu aynı satırda
        ImGui::SetCursorPosX(50);
        ImGui::SetWindowFontScale(2.0f);
        ImGui::Text("Projects");
        ImGui::SetWindowFontScale(1.0f);
        
        float windowWidth = ImGui::GetWindowWidth();
        ImGui::SameLine(windowWidth - 230);
        
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.8f, 1.0f));
        
        if (ImGui::Button("  New Project", ImVec2(180, 40)))
        {
            m_ShowNewProject = true;
        }
        
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        
        ImGui::SetCursorPosX(50);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Recent Projects - Projects ile hizalı (x=50)
        ImGui::SetCursorPosX(50);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 0.9f));
        ImGui::Text("Recent Projects");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        
        if (m_RecentProjects.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));
            ImGui::TextWrapped("No recent projects. Create a new project to get started!");
            ImGui::PopStyleColor();
        }
        else
        {
            float windowWidth = ImGui::GetWindowWidth();
            float removeBtnW = 80.0f;
            float removeBtnRight = windowWidth - 50.0f;
            float removeBtnX = removeBtnRight - removeBtnW;
            float selectW = removeBtnX - 50.0f - 10.0f;

            for (size_t i = 0; i < m_RecentProjects.size(); i++)
            {
                const auto& project = m_RecentProjects[i];
                
                ImGui::PushID((int)i);
                
                ImGui::SetCursorPosX(50);
                ImGui::SetWindowFontScale(1.5f);
                bool clicked = ImGui::Selectable(project.Name.c_str(), false, 0, ImVec2(selectW, 77));
                ImGui::SetWindowFontScale(1.0f);
                
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Path: %s", project.Path.string().c_str());
                    ImGui::Text("Click to open");
                    ImGui::EndTooltip();
                }
                
                // Remove button - New Project ile hizalı
                ImGui::SameLine(removeBtnX);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 0.8f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                
                bool removeClicked = ImGui::Button("Remove", ImVec2(removeBtnW, 77));
                
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                ImGui::PopID();
                
                if (removeClicked)
                {
                    m_RecentProjects.erase(m_RecentProjects.begin() + i);
                    SaveRecentProjects();
                    break;
                }
                
                if (clicked)
                {
                    OpenProject(project);
                    break;
                }
                
                ImGui::Spacing();
            }
        }
    }

    void ProjectHub::RenderSettings()
    {
        ImGui::SetCursorPos(ImVec2(40, 40));
        
        ImGui::Text("Settings");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("Settings panel - Coming soon");
        ImGui::PopStyleColor();
    }
}
