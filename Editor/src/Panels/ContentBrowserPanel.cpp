#include "ContentBrowserPanel.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Project/Project.h"

#include <imgui.h>
#include <fstream>
#include <nfd.h>

namespace Conqueror::Editor
{
    ContentBrowserPanel::ContentBrowserPanel()
    {
        // Motor klasöründeki Assets (Fall back)
        m_BaseDirectory = std::filesystem::current_path() / "Assets";
        m_CurrentDirectory = m_BaseDirectory;
        
        // Icon'ları yükle
        LoadIcons();
    }

    void ContentBrowserPanel::RefreshContext()
    {
        auto project = Project::GetActive();
        if (project)
        {
            m_BaseDirectory = project->GetProjectDirectory();
            m_CurrentDirectory = project->GetAssetDirectory(); // Varsayılan olarak Assets seçili
        }
        else
        {
            m_BaseDirectory = std::filesystem::current_path();
            m_CurrentDirectory = m_BaseDirectory / "Assets";
        }

        m_SelectedPath.clear();
        
        // Klasörlerin varlığından emin ol
        if (!std::filesystem::exists(m_BaseDirectory / "Assets"))
            std::filesystem::create_directories(m_BaseDirectory / "Assets");
        if (!std::filesystem::exists(m_BaseDirectory / "Packages"))
            std::filesystem::create_directories(m_BaseDirectory / "Packages");
    }

    void ContentBrowserPanel::LoadIcons()
    {
        std::filesystem::path iconPath = std::filesystem::current_path() / "Resources" / "Icons";
        
        m_FolderIcon = Texture2D::Create((iconPath / "folder.png").string());
        m_FileIcon = Texture2D::Create((iconPath / "file.png").string());
        m_ScriptIcon = Texture2D::Create((iconPath / "script.png").string());
        m_ImageIcon = Texture2D::Create((iconPath / "image.png").string());
        m_SceneIcon = Texture2D::Create((iconPath / "scene.png").string());
        
        CQ_CORE_INFO("ContentBrowser: Icons loaded");
    }

    void ContentBrowserPanel::OnImGuiRender()
    {
        ImGui::Begin("Content Browser");

        if (!std::filesystem::exists(m_CurrentDirectory))
        {
            ImGui::Text("Directory not found: %s", m_CurrentDirectory.string().c_str());
            RefreshContext();
            ImGui::End();
            return;
        }

        // Sol panel: Klasör ağacı
        ImGui::BeginChild("FolderTree", ImVec2(200, 0), true);
        
        RenderFolderTree(m_BaseDirectory / "Assets");
        RenderFolderTree(m_BaseDirectory / "Packages");

        ImGui::EndChild();

        ImGui::SameLine();

        // Sağ panel: İçerik
        ImGui::BeginChild("Content", ImVec2(0, 0), true);
        
        // Drag & Drop target (boş alana dosya bırakma)
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char* sourcePath = (const char*)payload->Data;
                std::filesystem::path source(sourcePath);
                std::filesystem::path dest = m_CurrentDirectory / source.filename();
                
                try
                {
                    if (source != dest && std::filesystem::exists(source))
                    {
                        if (std::filesystem::is_directory(source))
                        {
                            std::filesystem::copy(source, dest, std::filesystem::copy_options::recursive);
                        }
                        else
                        {
                            std::filesystem::copy_file(source, dest);
                        }
                        CQ_CORE_INFO("Moved: {0} -> {1}", source.string(), dest.string());
                    }
                }
                catch (const std::exception& e)
                {
                    CQ_CORE_ERROR("Failed to move: {0}", e.what());
                }
            }
            ImGui::EndDragDropTarget();
        }
        
        // Boş alana tıklayınca seçimi kaldır
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            m_SelectedPath.clear();
        }
        
        // Breadcrumb
        RenderBreadcrumb();
        ImGui::Separator();
        
        // Grid view
        RenderContentGrid();
        
        // Context menu
        RenderContextMenu();
        
        ImGui::EndChild();

        ImGui::End();
    }

    void ContentBrowserPanel::RenderFolderTree(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return;

        // Klasör ise tree node
        if (std::filesystem::is_directory(path))
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            
            if (path == m_CurrentDirectory)
                flags |= ImGuiTreeNodeFlags_Selected;

            std::string folderName = path.filename().string();
            bool opened = ImGui::TreeNodeEx(folderName.c_str(), flags);

            if (ImGui::IsItemClicked())
            {
                m_CurrentDirectory = path;
            }

            if (opened)
            {
                for (const auto& entry : std::filesystem::directory_iterator(path))
                {
                    RenderFolderTree(entry.path());
                }
                ImGui::TreePop();
            }
        }
        else
        {
            // Dosya ise leaf node
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            
            if (path == m_SelectedPath)
                flags |= ImGuiTreeNodeFlags_Selected;

            std::string fileName = GetFileNameWithoutExtension(path);
            ImGui::TreeNodeEx(fileName.c_str(), flags);

            if (ImGui::IsItemClicked())
            {
                m_SelectedPath = path;
            }
        }
    }

    void ContentBrowserPanel::RenderBreadcrumb()
    {
        std::vector<std::filesystem::path> pathParts;
        std::filesystem::path temp = m_CurrentDirectory;
        
        while (temp != m_BaseDirectory && temp.has_parent_path())
        {
            pathParts.push_back(temp);
            temp = temp.parent_path();
        }
        
        std::reverse(pathParts.begin(), pathParts.end());

        for (size_t i = 0; i < pathParts.size(); i++)
        {
            if (i > 0)
                ImGui::SameLine();

            std::string name = pathParts[i].filename().string();
            
            if (ImGui::Button(name.c_str()))
            {
                m_CurrentDirectory = pathParts[i];
            }

            if (i < pathParts.size() - 1)
            {
                ImGui::SameLine();
                ImGui::Text(">");
            }
        }
    }

    void ContentBrowserPanel::RenderContentGrid()
    {
        static float padding = 16.0f;
        static float thumbnailSize = 80.0f;
        float cellSize = thumbnailSize + padding;

        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1)
            columnCount = 1;

        ImGui::Columns(columnCount, 0, false);

        for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory))
        {
            const auto& path = entry.path();
            std::string displayName = GetFileNameWithoutExtension(path);
            std::shared_ptr<Texture2D> icon = GetFileIconTexture(path);
            bool isSelected = (m_SelectedPath == path);

            ImGui::PushID(path.string().c_str());
            
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            
            // İkon
            if (icon)
            {
                ImGui::Image((ImTextureID)(uint64_t)icon->GetRendererID(), 
                             ImVec2(thumbnailSize, thumbnailSize), 
                             ImVec2(0, 1), ImVec2(1, 0));
            }
            else
            {
                ImGui::Dummy(ImVec2(thumbnailSize, thumbnailSize));
            }
            
            // Invisible button overlay
            ImGui::SetCursorScreenPos(cursorPos);
            ImGui::InvisibleButton("##item", ImVec2(thumbnailSize, thumbnailSize));
            
            // Highlight seçili
            if (isSelected)
            {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(cursorPos, 
                                        ImVec2(cursorPos.x + thumbnailSize, cursorPos.y + thumbnailSize),
                                        IM_COL32(60, 120, 200, 100));
            }
            
            // Hover highlight
            if (ImGui::IsItemHovered())
            {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRect(cursorPos, 
                                  ImVec2(cursorPos.x + thumbnailSize, cursorPos.y + thumbnailSize),
                                  IM_COL32(100, 150, 255, 200), 0.0f, 0, 2.0f);
                
                ImGui::BeginTooltip();
                ImGui::Text("%s", path.filename().string().c_str());
                ImGui::EndTooltip();
            }
            
            // Tıklama
            if (ImGui::IsItemClicked())
            {
                m_SelectedPath = path;
            }
            
            // Çift tıklama
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (entry.is_directory())
                {
                    m_CurrentDirectory = path;
                }
                else
                {
                    OpenFile(path);
                }
            }
            
            // Sağ tık
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                m_SelectedPath = path;
                CQ_CORE_INFO("Right click on: {0}, opening popup", path.string());
            }
            
            // Sağ tık menü (item üzerinde)
            if (ImGui::BeginPopupContextItem("##ItemContext"))
            {
                CQ_CORE_INFO("Item context menu opened");
                
                if (ImGui::MenuItem("Open"))
                {
                    OpenFile(m_SelectedPath);
                }
                
                ImGui::Separator();
                
                if (ImGui::MenuItem("Rename"))
                {
                    m_IsRenaming = true;
                    memset(m_RenameBuffer, 0, sizeof(m_RenameBuffer));
                    std::string name = GetFileNameWithoutExtension(m_SelectedPath);
                    strcpy(m_RenameBuffer, name.c_str());
                }
                
                if (ImGui::MenuItem("Duplicate"))
                {
                    DuplicateSelected();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Delete"))
                {
                    DeleteSelected();
                }

                ImGui::EndPopup();
            }

            // Drag & Drop
            if (ImGui::BeginDragDropSource())
            {
                std::string itemPath = path.string();
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
                ImGui::Text("%s", displayName.c_str());
                ImGui::EndDragDropSource();
            }

            // İsim
            ImGui::TextWrapped("%s", displayName.c_str());

            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
    }

    void ContentBrowserPanel::OpenFile(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path) || std::filesystem::is_directory(path))
            return;

        std::string ext = path.extension().string();
        if (ext == ".cpp" || ext == ".h" || ext == ".cqs" || ext == ".cqsh" || ext == ".cqcont")
        {
            if (m_FileOpenCallback)
            {
                m_FileOpenCallback(path);
                return;
            }
        }

        std::string pathStr = path.string();
        
#ifdef CQ_PLATFORM_WINDOWS
        std::string command = "start \"\" \"" + pathStr + "\"";
#elif defined(CQ_PLATFORM_LINUX)
        std::string command = "xdg-open \"" + pathStr + "\"";
#else
        std::string command = "open \"" + pathStr + "\"";
#endif
        
        system(command.c_str());
        CQ_CORE_INFO("Opened file: {0}", pathStr);
    }

    void ContentBrowserPanel::DuplicateSelected()
    {
        if (m_SelectedPath.empty() || !std::filesystem::exists(m_SelectedPath))
            return;

        try
        {
            std::filesystem::path newPath = m_SelectedPath;
            std::string stem = newPath.stem().string();
            std::string ext = newPath.extension().string();
            std::filesystem::path parentPath = newPath.parent_path();
            
            int counter = 1;
            do
            {
                newPath = parentPath / (stem + "_copy" + (counter > 1 ? std::to_string(counter) : "") + ext);
                counter++;
            } while (std::filesystem::exists(newPath));
            
            if (std::filesystem::is_directory(m_SelectedPath))
            {
                std::filesystem::copy(m_SelectedPath, newPath, std::filesystem::copy_options::recursive);
            }
            else
            {
                std::filesystem::copy_file(m_SelectedPath, newPath);
            }
            
            CQ_CORE_INFO("Duplicated: {0} -> {1}", m_SelectedPath.string(), newPath.string());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to duplicate: {0}", e.what());
        }
    }

    void ContentBrowserPanel::RenderContextMenu()
    {
        // Boş alana sağ tık
        if (ImGui::BeginPopupContextWindow("ContentBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("New Folder"))
            {
                m_CreatingNewFolder = true;
                memset(m_NewItemNameBuffer, 0, sizeof(m_NewItemNameBuffer));
                strcpy(m_NewItemNameBuffer, "NewFolder");
            }

            if (ImGui::BeginMenu("New Script"))
            {
                if (ImGui::MenuItem("Native Script"))
                {
                    m_CreatingNewScript = true;
                    memset(m_NewItemNameBuffer, 0, sizeof(m_NewItemNameBuffer));
                    strcpy(m_NewItemNameBuffer, "NewScript");
                }

                if (ImGui::MenuItem("Conqueror Script"))
                {
                    m_CreatingConquerorScript = true;
                    memset(m_NewItemNameBuffer, 0, sizeof(m_NewItemNameBuffer));
                    strcpy(m_NewItemNameBuffer, "NewScript");
                }

                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Shader"))
            {
                m_CreatingShader = true;
                memset(m_NewItemNameBuffer, 0, sizeof(m_NewItemNameBuffer));
                strcpy(m_NewItemNameBuffer, "NewShader");
            }

            if (ImGui::MenuItem("Empty File"))
            {
                m_CreatingEmptyFile = true;
                memset(m_NewItemNameBuffer, 0, sizeof(m_NewItemNameBuffer));
                strcpy(m_NewItemNameBuffer, "NewFile");
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Import File"))
            {
                ImportFile();
            }

            ImGui::EndPopup();
        }

        // New folder dialog
        if (m_CreatingNewFolder)
        {
            ImGui::OpenPopup("New Folder");
            m_CreatingNewFolder = false;
        }

        if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Folder Name:");
            ImGui::InputText("##FolderName", m_NewItemNameBuffer, sizeof(m_NewItemNameBuffer));

            if (ImGui::Button("Create"))
            {
                CreateNewFolder();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // New script dialog
        if (m_CreatingNewScript)
        {
            ImGui::OpenPopup("New Script");
            m_CreatingNewScript = false;
        }

        if (ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Script Name:");
            ImGui::InputText("##ScriptName", m_NewItemNameBuffer, sizeof(m_NewItemNameBuffer));

            if (ImGui::Button("Create"))
            {
                CreateNewScript();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // New ConquerorScript dialog
        if (m_CreatingConquerorScript)
        {
            ImGui::OpenPopup("New ConquerorScript");
            m_CreatingConquerorScript = false;
        }

        if (ImGui::BeginPopupModal("New ConquerorScript", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Script Name:");
            ImGui::InputText("##ConquerorScriptName", m_NewItemNameBuffer, sizeof(m_NewItemNameBuffer));

            if (ImGui::Button("Create"))
            {
                CreateConquerorScript();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // New Shader dialog
        if (m_CreatingShader)
        {
            ImGui::OpenPopup("New Shader");
            m_CreatingShader = false;
        }

        if (ImGui::BeginPopupModal("New Shader", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Shader Name:");
            ImGui::InputText("##ShaderName", m_NewItemNameBuffer, sizeof(m_NewItemNameBuffer));

            if (ImGui::Button("Create"))
            {
                CreateShader();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // New Empty File dialog
        if (m_CreatingEmptyFile)
        {
            ImGui::OpenPopup("New Empty File");
            m_CreatingEmptyFile = false;
        }

        if (ImGui::BeginPopupModal("New Empty File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("File Name (with extension):");
            ImGui::InputText("##EmptyFileName", m_NewItemNameBuffer, sizeof(m_NewItemNameBuffer));

            if (ImGui::Button("Create"))
            {
                CreateEmptyFile();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // Rename dialog
        if (m_IsRenaming)
        {
            ImGui::OpenPopup("Rename");
            m_IsRenaming = false;
        }

        if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("New Name:");
            ImGui::InputText("##RenameName", m_RenameBuffer, sizeof(m_RenameBuffer));

            if (ImGui::Button("Rename"))
            {
                RenameSelected();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void ContentBrowserPanel::CreateNewFolder()
    {
        std::filesystem::path newPath = m_CurrentDirectory / m_NewItemNameBuffer;
        
        if (std::filesystem::exists(newPath))
        {
            CQ_CORE_WARN("Folder already exists: {0}", newPath.string());
            return;
        }

        try
        {
            std::filesystem::create_directory(newPath);
            CQ_CORE_INFO("Created folder: {0}", newPath.string());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to create folder: {0}", e.what());
        }
    }

    void ContentBrowserPanel::CreateNewScript()
    {
        std::string scriptName = m_NewItemNameBuffer;
        std::filesystem::path newPath = m_CurrentDirectory / (scriptName + ".cpp");
        
        if (std::filesystem::exists(newPath))
        {
            CQ_CORE_WARN("Script already exists: {0}", newPath.string());
            return;
        }

        try
        {
            std::ofstream file(newPath);
            file << "// " << scriptName << "\n\n";
            file << "#include <iostream>\n\n";
            file << "void " << scriptName << "()\n";
            file << "{\n";
            file << "    // TODO: Implement\n";
            file << "}\n";
            file.close();
            
            CQ_CORE_INFO("Created script: {0}", newPath.string());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to create script: {0}", e.what());
        }
    }

    void ContentBrowserPanel::CreateConquerorScript()
    {
        std::string scriptName = m_NewItemNameBuffer;
        std::filesystem::path newPath = m_CurrentDirectory / (scriptName + ".cqs");
        
        if (std::filesystem::exists(newPath))
        {
            CQ_CORE_WARN("ConquerorScript already exists: {0}", newPath.string());
            return;
        }

        try
        {
            std::ofstream file(newPath);
            file << "using Conqueror;\n\n";
            file << "script " << scriptName << " : ConquerorScript\n";
            file << "{\n";
            file << "    func OnCreate()\n";
            file << "    {\n";
            file << "        LogInfo(\"Hello World!\");\n";
            file << "    }\n";
            file << "}\n";
            file.close();
            
            CQ_CORE_INFO("Created ConquerorScript: {0}", newPath.string());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to create ConquerorScript: {0}", e.what());
        }
    }

    void ContentBrowserPanel::CreateShader()
    {
        std::string shaderName = m_NewItemNameBuffer;
        std::filesystem::path newPath = m_CurrentDirectory / (shaderName + ".cqsh");
        
        if (std::filesystem::exists(newPath))
        {
            CQ_CORE_WARN("Shader already exists: {0}", newPath.string());
            return;
        }

        try
        {
            std::ofstream file(newPath);
            file << "#language GLSL\n\n";
            file << "#type vertex\n";
            file << "#version 460 core\n\n";
            file << "layout(location = 0) in vec3 a_Position;\n\n";
            file << "uniform mat4 u_ViewProjection;\n";
            file << "uniform mat4 u_Transform;\n\n";
            file << "void main()\n";
            file << "{\n";
            file << "    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);\n";
            file << "}\n\n";
            file << "#type fragment\n";
            file << "#version 460 core\n\n";
            file << "layout(location = 0) out vec4 color;\n\n";
            file << "void main()\n";
            file << "{\n";
            file << "    color = vec4(1.0);\n";
            file << "}\n";
            file.close();
            
            CQ_CORE_INFO("Created Shader: {0}", newPath.string());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to create Shader: {0}", e.what());
        }
    }

    void ContentBrowserPanel::CreateEmptyFile()
    {
        std::string fileName = m_NewItemNameBuffer;
        std::filesystem::path newPath = m_CurrentDirectory / fileName;
        
        if (std::filesystem::exists(newPath))
        {
            CQ_CORE_WARN("File already exists: {0}", newPath.string());
            return;
        }

        try
        {
            std::ofstream file(newPath);
            file << "//File\n";
            file.close();
            
            CQ_CORE_INFO("Created empty file: {0}", newPath.string());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to create empty file: {0}", e.what());
        }
    }

    void ContentBrowserPanel::DeleteSelected()
    {
        if (m_SelectedPath.empty() || !std::filesystem::exists(m_SelectedPath))
            return;

        try
        {
            if (std::filesystem::is_directory(m_SelectedPath))
            {
                std::filesystem::remove_all(m_SelectedPath);
            }
            else
            {
                std::filesystem::remove(m_SelectedPath);
            }
            
            CQ_CORE_INFO("Deleted: {0}", m_SelectedPath.string());
            m_SelectedPath.clear();
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to delete: {0}", e.what());
        }
    }

    void ContentBrowserPanel::RenameSelected()
    {
        if (m_SelectedPath.empty() || !std::filesystem::exists(m_SelectedPath))
            return;

        std::string newName = m_RenameBuffer;
        
        // Dosya ise uzantıyı koru
        if (!std::filesystem::is_directory(m_SelectedPath))
        {
            newName += m_SelectedPath.extension().string();
        }

        std::filesystem::path newPath = m_SelectedPath.parent_path() / newName;
        
        if (std::filesystem::exists(newPath))
        {
            CQ_CORE_WARN("File already exists: {0}", newPath.string());
            return;
        }

        try
        {
            std::filesystem::rename(m_SelectedPath, newPath);
            CQ_CORE_INFO("Renamed: {0} -> {1}", m_SelectedPath.string(), newPath.string());
            m_SelectedPath = newPath;
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to rename: {0}", e.what());
        }
    }

    std::shared_ptr<Texture2D> ContentBrowserPanel::GetFileIconTexture(const std::filesystem::path& path)
    {
        if (std::filesystem::is_directory(path))
            return m_FolderIcon;

        std::string ext = path.extension().string();
        
        if (ext == ".cpp" || ext == ".h" || ext == ".cqs")
            return m_ScriptIcon;
        else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            return m_ImageIcon;
        else if (ext == ".cqscene")
            return m_SceneIcon;
        else if (ext == ".cqcont")
            return m_SceneIcon;
        else
            return m_FileIcon;
    }

    std::string ContentBrowserPanel::GetFileIcon(const std::filesystem::path& path)
    {
        if (std::filesystem::is_directory(path))
            return "[DIR]";

        std::string ext = path.extension().string();
        
        if (ext == ".cpp" || ext == ".h")
            return "[CPP]";
        else if (ext == ".cqs")
            return "[CQS]";
        else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            return "[IMG]";
        else if (ext == ".cqscene")
            return "[SCN]";
        else if (ext == ".cqcont")
            return "[ANM]";
        else
            return "[FILE]";
    }

    std::string ContentBrowserPanel::GetFileNameWithoutExtension(const std::filesystem::path& path)
    {
        if (std::filesystem::is_directory(path))
            return path.filename().string();
        
        return path.stem().string();
    }

    void ContentBrowserPanel::ImportFile()
    {
        nfdu8char_t* outPath = nullptr;
        nfdresult_t result = NFD_OpenDialogU8(&outPath, nullptr, 0, nullptr);
        
        if (result == NFD_OKAY)
        {
            std::filesystem::path sourcePath(outPath);
            std::filesystem::path destPath = m_CurrentDirectory / sourcePath.filename();
            
            try
            {
                // Aynı isimde dosya varsa yeni isim oluştur
                if (std::filesystem::exists(destPath))
                {
                    std::string stem = destPath.stem().string();
                    std::string ext = destPath.extension().string();
                    int counter = 1;
                    
                    do
                    {
                        destPath = m_CurrentDirectory / (stem + "_" + std::to_string(counter) + ext);
                        counter++;
                    } while (std::filesystem::exists(destPath));
                }
                
                std::filesystem::copy_file(sourcePath, destPath);
                CQ_CORE_INFO("Imported file: {0} -> {1}", sourcePath.string(), destPath.string());
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("Failed to import file: {0}", e.what());
            }
            
            NFD_FreePathU8(outPath);
        }
        else if (result == NFD_CANCEL)
        {
            CQ_CORE_INFO("Import cancelled");
        }
        else
        {
            CQ_CORE_ERROR("NFD Error: {0}", NFD_GetError());
        }
    }
}
