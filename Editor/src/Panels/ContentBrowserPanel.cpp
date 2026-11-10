#include "Scripting/DataAssets.h"
#include "Core/Math/CurveAsset.h"
#include "Renderer/CubemapAsset.h"
#include "Core/Animation/AvatarMask.h"
#include "Core/Animation/PropertyAnimation.h"
#include "Core/Input/InputActionAssets.h"
#include "Scene/Signals.h"
#include "Terrain/Terrain.h"
#include "Core/Audio/AudioRandomContainer.h"
#include "Core/Audio/AudioMixer.h"
#include "Renderer/ShaderVariantCollection.h"
#include "Assets/ShaderTemplates.h"
#include "Renderer/CustomRenderTexture.h"
#include "Scene/Billboards.h"
#include "Renderer/RenderSettings.h"
#include "Renderer/Lightmap/LightmapParameters.h"
#include "Scene/PostProcessVolume.h"
#include "Renderer/SpriteAtlas.h"
#include "Grid/GridSystem.h"
#include "Renderer/SpriteSheet.h"
#include "Renderer/RHI/TextureSettings.h"
#include "Assets/SceneTemplate.h"
#include "Renderer/RenderTargetRegistry.h"
#include "Core/PhysicsSystem/PhysicsMaterial.h"
#include "Timeline/Timeline.h"
#include "Scripting/Visual/VisualGraph.h"
#include "Panels/MaterialEditorUI.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "ContentBrowserPanel.h"
#include "NodeAssetPanel.h"
#include "Assets/AssetPreview.h"
#include "UI/UIToolkit.h"
#include "Particles/ParticleSystem.h"
#include "AISystem/EQS/EnvQuery.h"
#include "Scripting/TestRunner.h"
#include "Scripting/Visual/VisualGraph.h"
#include "Core/Assets/SimpleAsset.h"
#include "Assets/SimpleAssetInspector.h"
#include "Assets/SceneTemplate.h"
#include "EditorState.h"
#include <sstream>
#include "Scripting/ScriptRuntime.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Animation/AnimationController.h"
#include "Assets/AnimatorOverrideInspector.h"
#include "Core/Animation/BlendSpace.h"
#include <fstream>
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"
#include "Scene/Scene.h"
#include "UI/EditorUI.h"
#include "AssetActions.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Scene/Components.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <nfd.h>

#include <algorithm>
#include <unordered_map>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace Conqueror::Editor
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        std::string FormatSize(uintmax_t bytes)
        {
            char buf[32];
            if (bytes < 1024) snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
            else if (bytes < 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
            else if (bytes < 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
            else snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
            return buf;
        }

        // Metni genişliğe sığdır (sonuna "...")
        std::string Ellipsize(const std::string& text, float maxWidth)
        {
            if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
                return text;
            std::string s = text;
            while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > maxWidth)
            {
                // UTF-8 güvenli kırpma
                do { s.pop_back(); } while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80);
            }
            return s + "...";
        }

        bool IsHidden(const fs::path& p)
        {
            // İçe aktarma ayarları (.meta) ve sprite dilimleri (.cqsprite) yan dosyaları gizli (Unity gibi)
            if (p.extension() == ".meta" || p.extension() == ".cqsprite")
                return true;
            // Sprite Atlas paketi ("<atlas>.cqspriteatlas.png"): varlığın parçası
            if (p.filename().string().find(".cqspriteatlas.") != std::string::npos)
                return true;
            std::string name = p.filename().string();
            return name.empty() || name[0] == '.';
        }
    }

    // ------------------------------------------------------------------ Varlık türleri

    namespace
    {
        // Her karede dosya okumamak için (değişim zamanıyla önbellekli)
        bool IsPrefabVariantCached(const fs::path& path)
        {
            static std::unordered_map<std::string, std::pair<fs::file_time_type, bool>> cache;
            std::error_code ec;
            const auto t = fs::last_write_time(path, ec);
            auto& entry = cache[path.string()];
            if (entry.first != t || ec)
                entry = { t, PrefabSerializer::IsVariant(path) };
            return entry.second;
        }
    }

    ContentBrowserPanel::AssetTypeInfo ContentBrowserPanel::GetAssetTypeInfo(const fs::path& path, bool isDirectory)
    {
        if (isDirectory)
            return { ICON_FA_FOLDER, IM_COL32(232, 184, 76, 255), "Folder" };

        const std::string ext = ToLower(path.extension().string());
        if (ext == UIToolkit::DocumentExtension) return { ICON_FA_WINDOW_MAXIMIZE, IM_COL32(120, 200, 255, 255), "UI Document" };
        if (ext == UIToolkit::StyleExtension) return { ICON_FA_FILE_LINES, IM_COL32(120, 200, 255, 255), "Style Sheet" };
        if (ext == ParticleAssets::Extension) return { ICON_FA_FIRE, IM_COL32(255, 150, 90, 255), "Particle System" };
        if (ext == ".cqdatatable") return { ICON_FA_TABLE, IM_COL32(150, 220, 160, 255), "Data Table" };
        if (ext == ".cqcurvetable") return { ICON_FA_CHART_LINE, IM_COL32(120, 220, 210, 255), "Curve Table" };
        if (ext == ".cqeqs") return { ICON_FA_LOCATION_CROSSHAIRS, IM_COL32(120, 180, 255, 255), "Environment Query" };
        if (ext == ".cqplayable") return { ICON_FA_FILE_CODE, IM_COL32(140, 185, 100, 255), "Playable Asset" };
        if (const SimpleAssets::Def* simple = SimpleAssets::ForExtension(ext))
            return { simple->Icon, simple->Color, simple->Label.c_str() };
        if (const NodeAssetSchema* graph = NodeAssetPanel::SchemaForExtension(ext))
            return { graph->Icon.c_str(), graph->Color, graph->Title.c_str() };
        if (ext == ".cqmatfunc") return { ICON_FA_DIAGRAM_PROJECT, IM_COL32(190, 120, 255, 255), "Material Function" };
        if (ext == ".cqmat") return { ICON_FA_CIRCLE_HALF_STROKE, IM_COL32(235, 120, 190, 255), "Material" };
        if (ext == ".cqphysmat") return { ICON_FA_BASKETBALL, IM_COL32(120, 200, 120, 255), "Physics Material" };
        if (ext == ".cqmesh") return { ICON_FA_CUBE, IM_COL32(150, 190, 235, 255), "Mesh" };
        if (ext == ".cqlighting") return { ICON_FA_LIGHTBULB, IM_COL32(255, 210, 110, 255), "Lighting Settings" };
        if (ext == ".cqlmparams") return { ICON_FA_SLIDERS, IM_COL32(255, 210, 110, 255), "Lightmap Parameters" };
        if (ext == ".cqsignal") return { ICON_FA_TOWER_BROADCAST, IM_COL32(240, 190, 90, 255), "Signal" };
        if (ext == ".cqinputactions") return { ICON_FA_GAMEPAD, IM_COL32(120, 200, 255, 255), "Input Actions" };
        if (ext == ".cqdata") return { ICON_FA_DATABASE, IM_COL32(150, 220, 160, 255), "Data Asset" };
        if (ext == ".cqcurve") return { ICON_FA_CHART_LINE, IM_COL32(120, 220, 210, 255), "Curve" };
        if (ext == ".cqcubemap") return { ICON_FA_CUBE, IM_COL32(140, 190, 255, 255), "Cubemap" };
        if (ext == ".cqmask") return { ICON_FA_PERSON, IM_COL32(120, 210, 140, 255), "Avatar Mask" };
        if (ext == ".cqanim") return { ICON_FA_CLAPPERBOARD, IM_COL32(110, 196, 240, 255), "Animation Clip" };
        if (ext == ".cqterrainlayer") return { ICON_FA_LAYER_GROUP, IM_COL32(150, 200, 110, 255), "Terrain Layer" };
        if (ext == ".cqbrush") return { ICON_FA_PAINTBRUSH, IM_COL32(150, 200, 110, 255), "Brush" };
        if (ext == ".cqaudiorandom") return { ICON_FA_SHUFFLE, IM_COL32(255, 170, 90, 255), "Audio Random Container" };
        if (ext == ".cqmixer") return { ICON_FA_SLIDERS, IM_COL32(255, 170, 90, 255), "Audio Mixer" };
        if (ext == ".cqraytrace") return { ICON_FA_WAND_MAGIC_SPARKLES, IM_COL32(190, 120, 255, 255), "Ray Tracing Shader" };
        if (ext == ".cqshadervariants") return { ICON_FA_LAYER_GROUP, IM_COL32(190, 120, 255, 255), "Shader Variant Collection" };
        if (ext == ".cqcrt") return { ICON_FA_TV, IM_COL32(130, 200, 250, 255), "Custom Render Texture" };
        if (ext == ".cqflare") return { ICON_FA_STAR, IM_COL32(255, 220, 120, 255), "Lens Flare" };
        if (ext == ".cqvolume") return { ICON_FA_CUBES_STACKED, IM_COL32(160, 180, 255, 255), "Volume Profile" };
        if (ext == ".cqspriteatlas") return { ICON_FA_OBJECT_GROUP, IM_COL32(120, 200, 255, 255), "Sprite Atlas" };
        if (ext == ".cqtile") return { ICON_FA_SQUARE, IM_COL32(120, 210, 170, 255), "Tile" };
        if (ext == ".cqpalette") return { ICON_FA_SWATCHBOOK, IM_COL32(120, 210, 170, 255), "Tile Palette" };
        if (ext == ".cqphysmat2d") return { ICON_FA_BASKETBALL, IM_COL32(120, 220, 200, 255), "Physics Material 2D" };
        if (ext == ".cqrt") return { ICON_FA_TV, IM_COL32(110, 190, 240, 255), "Render Texture" };
        if (ext == ".cqtimeline") return { ICON_FA_FILM, IM_COL32(240, 190, 90, 255), "Timeline" };
        if (ext == ".cqscene") return { ICON_FA_CLAPPERBOARD, IM_COL32(90, 170, 255, 255), "Scene" };
        if (ext == ".cqscenetemplate") return { ICON_FA_CLAPPERBOARD, IM_COL32(150, 200, 255, 255), "Scene Template" };
        if (ext == ".cqprefab")
            return IsPrefabVariantCached(path) ? AssetTypeInfo{ ICON_FA_CUBES_STACKED, IM_COL32(170, 140, 255, 255), "Prefab Variant" }
                                                     : AssetTypeInfo{ ICON_FA_CUBES_STACKED, IM_COL32(110, 175, 255, 255), "Prefab" };
        if (ext == ".cqgraph") return { ICON_FA_DIAGRAM_PROJECT, IM_COL32(120, 220, 200, 255), "Visual Script" };
        if (ext == ".cqcont") return { ICON_FA_PERSON_RUNNING, IM_COL32(255, 120, 160, 255), "Animator Controller" };
        if (ext == ".cqblendspace") return { ICON_FA_BRAILLE, IM_COL32(255, 150, 190, 255), "Blend Space" };
        if (ext == ".cqoverride") return { ICON_FA_PERSON_RUNNING, IM_COL32(255, 170, 120, 255), "Animator Override Controller" };
        if (ext == ".cqmontage") return { ICON_FA_PERSON_RUNNING, IM_COL32(255, 160, 110, 255), "Anim Montage" };
        if (ext == ".cqsh" || ext == ".glsl" || ext == ".hlsl" || ext == ".vert" || ext == ".frag" || ext == ".comp")
            return { ICON_FA_WAND_MAGIC_SPARKLES, IM_COL32(190, 120, 255, 255), "Shader" };
        if (ext == ".cqs") return { ICON_FA_FILE_CODE, IM_COL32(120, 210, 140, 255), "Conqueror Script" };
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".cc")
            return { ICON_FA_FILE_CODE, IM_COL32(90, 150, 230, 255), "C++ Source" };
        if (ext == ".cs") return { ICON_FA_FILE_CODE, IM_COL32(150, 110, 230, 255), "C# Script" };
        if (ext == ".luaq") return { ICON_FA_FILE_CODE, IM_COL32(150, 80, 230, 255), "LuaQ Script" };
        if (ext == ".lua" || ext == ".luau") return { ICON_FA_FILE_CODE, IM_COL32(70, 90, 220, 255), "Luau Script" };
        if (ext == ".java") return { ICON_FA_FILE_CODE, IM_COL32(230, 120, 60, 255), "Java Source" };
        if (ext == ".py") return { ICON_FA_FILE_CODE, IM_COL32(240, 200, 70, 255), "Python Script" };
        if (ext == ".java") return { ICON_FA_FILE_CODE, IM_COL32(230, 120, 60, 255), "Java Source" };
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr" || ext == ".psd" || ext == ".gif")
            return { ICON_FA_IMAGE, IM_COL32(80, 200, 200, 255), "Texture" };
        if (ext == ".glb" || ext == ".gltf" || ext == ".fbx" || ext == ".obj" || ext == ".dae" || ext == ".blend" || ext == ".3ds")
            return { ICON_FA_CUBES, IM_COL32(120, 160, 230, 255), "Model" };
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
            return { ICON_FA_MUSIC, IM_COL32(255, 150, 60, 255), "Audio Clip" };
        if (ext == ".mp4" || ext == ".webm" || ext == ".mov" || ext == ".avi")
            return { ICON_FA_FILM, IM_COL32(230, 90, 90, 255), "Video" };
        if (ext == ".ttf" || ext == ".otf") return { ICON_FA_FONT, IM_COL32(200, 200, 200, 255), "Font" };
        if (ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".xml" || ext == ".ini" || ext == ".cqproj")
            return { ICON_FA_FILE_LINES, IM_COL32(170, 175, 185, 255), "Data" };
        if (ext == ".txt" || ext == ".md") return { ICON_FA_FILE_LINES, IM_COL32(170, 175, 185, 255), "Text" };
        return { ICON_FA_FILE, IM_COL32(150, 155, 165, 255), "File" };
    }

    bool ContentBrowserPanel::IsImage(const std::string& ext) const
    {
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
    }

    // ------------------------------------------------------------------ Temel

    ContentBrowserPanel::ContentBrowserPanel()
    {
        m_BaseDirectory = fs::current_path();
        m_CurrentDirectory = m_BaseDirectory / "Assets";
    }

    void ContentBrowserPanel::RefreshContext()
    {
        auto project = Project::GetActive();
        if (project)
        {
            m_BaseDirectory = project->GetProjectDirectory();
            m_CurrentDirectory = project->GetAssetDirectory();
        }
        else
        {
            m_BaseDirectory = fs::current_path();
            m_CurrentDirectory = m_BaseDirectory / "Assets";
        }

        std::error_code ec;
        fs::create_directories(m_BaseDirectory / "Assets", ec);
        fs::create_directories(m_BaseDirectory / "Packages", ec);

        m_SelectedPath.clear();
        m_RenamingPath.clear();
        m_BackStack.clear();
        m_ForwardStack.clear();
        m_Thumbnails.clear();
        m_Dirty = true;
    }

    // Motorla gelen paketler (Resources/Packages): Content Browser'da proje Packages klasörünün altında, salt okunur
    static const fs::path& EnginePackagesDir()
    {
        static const fs::path dir = fs::path(SceneSerializer::ResolveAssetPath("CQ:Resources/Packages")).lexically_normal();
        return dir;
    }

    static bool IsUnder(const fs::path& path, const fs::path& root)
    {
        const std::string p = path.lexically_normal().generic_string(), r = root.lexically_normal().generic_string();
        return !r.empty() && (p == r || (p.size() > r.size() && p.compare(0, r.size(), r) == 0 && p[r.size()] == '/'));
    }

    // Salt okunur: motorla gelen paketler ve projenin Packages klasörü (Unity gibi: Packages içine + ile varlık oluşturulmaz,
    // yapıştırılmaz, taşınmaz, silinmez, yeniden adlandırılmaz)
    bool ContentBrowserPanel::IsReadOnly(const fs::path& path)
    {
        if (IsUnder(path, EnginePackagesDir()))
            return true;
        const fs::path project = Project::GetActiveProjectDirectory();
        return !project.empty() && IsUnder(path, project / "Packages");
    }

    fs::path ContentBrowserPanel::WritableDirectory() const
    {
        if (!IsReadOnly(m_CurrentDirectory))
            return m_CurrentDirectory;
        return Project::GetActive() ? Project::GetAssetDirectory() : m_BaseDirectory / "Assets";
    }

    void ContentBrowserPanel::NavigateTo(const fs::path& directory, bool recordHistory)
    {
        // Unity: klasöre gitmek arama / başvuru süzgecini kapatır
        m_ResultsTitle.clear();
        m_Results.clear();
        std::error_code ec;
        if (!fs::is_directory(directory, ec) || directory == m_CurrentDirectory)
            return;
        if (recordHistory)
        {
            m_BackStack.push_back(m_CurrentDirectory);
            m_ForwardStack.clear();
        }
        m_CurrentDirectory = directory;
        m_SelectedPath.clear();
        m_RenamingPath.clear();
        m_Dirty = true;
    }

    void ContentBrowserPanel::RefreshEntries(bool force)
    {
        auto now = std::chrono::steady_clock::now();
        if (!force && !m_Dirty && m_EntriesDirectory == m_CurrentDirectory && now - m_LastRefresh < std::chrono::milliseconds(1000))
            return;

        m_Entries.clear();
        m_EntriesDirectory = m_CurrentDirectory;
        m_LastRefresh = now;
        m_Dirty = false;

        std::error_code ec;
        for (fs::directory_iterator it(m_CurrentDirectory, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        {
            const fs::path& p = it->path();
            if (IsHidden(p))
                continue;
            Entry e;
            e.Path = p;
            std::error_code ec2;
            e.IsDirectory = it->is_directory(ec2);
            e.Name = e.IsDirectory ? p.filename().string() : p.stem().string();
            e.Extension = ToLower(p.extension().string());
            if (!e.IsDirectory)
                e.Size = it->file_size(ec2);
            e.WriteTime = it->last_write_time(ec2);
            m_Entries.push_back(std::move(e));
        }

        // Önce klasörler, sonra ada göre (büyük/küçük harf duyarsız)
        std::sort(m_Entries.begin(), m_Entries.end(), [](const Entry& a, const Entry& b) {
            if (a.IsDirectory != b.IsDirectory)
                return a.IsDirectory;
            return ToLower(a.Path.filename().string()) < ToLower(b.Path.filename().string());
        });
    }

    std::shared_ptr<Texture2D> ContentBrowserPanel::GetThumbnail(const Entry& entry)
    {
        if (AssetPreview::CanPreview(entry.Path))
            return AssetPreview::Get(entry.Path);
        if (!IsImage(entry.Extension) || entry.Size > 64ull * 1024 * 1024)
            return nullptr;

        auto key = entry.Path.string();
        auto it = m_Thumbnails.find(key);
        if (it != m_Thumbnails.end() && it->second.WriteTime == entry.WriteTime)
            return it->second.Failed ? nullptr : it->second.Texture;

        // Kare başına sınırlı yükleme (büyük klasörlerde takılmasın)
        if (m_ThumbnailLoadsThisFrame >= 2)
            return nullptr;
        m_ThumbnailLoadsThisFrame++;

        Thumbnail thumb;
        thumb.WriteTime = entry.WriteTime;
        try
        {
            thumb.Texture = Texture2D::Create(key);
            thumb.Failed = !thumb.Texture || thumb.Texture->GetRendererID() == 0 || thumb.Texture->GetWidth() == 0;
        }
        catch (...)
        {
            thumb.Failed = true;
        }
        m_Thumbnails[key] = thumb;
        return thumb.Failed ? nullptr : thumb.Texture;
    }

    // ------------------------------------------------------------------ Çizim

    void ContentBrowserPanel::OnImGuiRender()
    {
        DrawPropertyWindows();
        if (!ImGui::Begin("Content Browser"))
        {
            m_Focused = false;
            ImGui::End();
            return;
        }
        m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        m_ThumbnailLoadsThisFrame = 0;

        std::error_code ec;
        if (!fs::exists(m_CurrentDirectory, ec))
        {
            RefreshContext();
            if (!fs::exists(m_CurrentDirectory, ec))
            {
                ImGui::TextDisabled("Project directory not found: %s", m_CurrentDirectory.string().c_str());
                ImGui::End();
                return;
            }
        }

        RefreshEntries();
        DrawToolbar();

        // Sol: klasör ağacı (yeniden boyutlandırılabilir)
        const float height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() - 2.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.12f));
        ImGui::BeginChild("##FolderTree", ImVec2(m_TreeWidth, height), ImGuiChildFlags_None);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        DrawFolderTree(m_BaseDirectory / "Assets", 0);
        DrawFolderTree(m_BaseDirectory / "Packages", 0);
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Ayırıcı (sürüklenebilir)
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##TreeSplitter", ImVec2(5.0f, height));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            m_TreeWidth = std::clamp(m_TreeWidth + ImGui::GetIO().MouseDelta.x, 120.0f, 600.0f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(ImGui::GetItemRectMin().x + 2.0f, ImGui::GetItemRectMin().y),
                                            ImVec2(ImGui::GetItemRectMin().x + 2.0f, ImGui::GetItemRectMax().y),
                                            ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
        ImGui::SameLine(0.0f, 0.0f);

        // Sağ: içerik
        ImGui::BeginChild("##Content", ImVec2(0, height), ImGuiChildFlags_None);
        DrawContent();
        ImGui::EndChild();

        DrawStatusBar();
        DrawModals();
        DrawPackageWindows();
        if (m_Focused)
            HandleKeyboard();
        if (!m_DroppedFiles.empty())
            ImportDroppedFiles();

        ImGui::End();
    }

    void ContentBrowserPanel::DrawToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        auto iconButton = [](const char* icon, const char* tooltip, bool enabled = true) {
            ImGui::BeginDisabled(!enabled);
            bool pressed = ImGui::Button(icon);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tooltip);
            return pressed;
        };

        // "+▾" (Unity Project penceresi): Create menüsünü doğrudan açar
        if (ImGui::Button(ICON_FA_PLUS " " ICON_FA_CARET_DOWN "##CreateMenu"))
            ImGui::OpenPopup("##CreateAsset");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Create");
        if (ImGui::BeginPopup("##CreateAsset"))
        {
            DrawCreateMenu();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (iconButton(ICON_FA_FILE_IMPORT, "Import file into this folder"))
            ImportFile();
        ImGui::SameLine();

        // Gezinme
        if (iconButton(ICON_FA_ARROW_LEFT, "Back", !m_BackStack.empty()))
        {
            m_ForwardStack.push_back(m_CurrentDirectory);
            fs::path target = m_BackStack.back();
            m_BackStack.pop_back();
            NavigateTo(target, false);
        }
        ImGui::SameLine();
        if (iconButton(ICON_FA_ARROW_RIGHT, "Forward", !m_ForwardStack.empty()))
        {
            m_BackStack.push_back(m_CurrentDirectory);
            fs::path target = m_ForwardStack.back();
            m_ForwardStack.pop_back();
            NavigateTo(target, false);
        }
        ImGui::SameLine();
        if (iconButton(ICON_FA_ARROW_UP, "Up one folder", m_CurrentDirectory != m_BaseDirectory && m_CurrentDirectory.has_parent_path() &&
                                                           m_CurrentDirectory.parent_path() != m_BaseDirectory.parent_path()))
            NavigateTo(m_CurrentDirectory.parent_path() == EnginePackagesDir() ? m_BaseDirectory / "Packages" : m_CurrentDirectory.parent_path());
        ImGui::SameLine();
        if (iconButton(ICON_FA_ARROWS_ROTATE, "Refresh"))
        {
            m_Thumbnails.clear();
            m_Dirty = true;
        }
        ImGui::SameLine();

        // Konum çubuğu (breadcrumb)
        {
            std::vector<fs::path> parts;
            const bool inEngine = IsReadOnly(m_CurrentDirectory);
            for (fs::path p = m_CurrentDirectory; ; p = p.parent_path())
            {
                // Motor paketi: "<proje> > Packages > Core > ..." gibi gösterilir
                if (inEngine && p == EnginePackagesDir())
                {
                    parts.push_back(m_BaseDirectory / "Packages");
                    parts.push_back(m_BaseDirectory);
                    break;
                }
                parts.push_back(p);
                if (p == m_BaseDirectory || !p.has_parent_path() || p.parent_path() == p)
                    break;
            }
            std::reverse(parts.begin(), parts.end());
            const float rightReserve = 330.0f;
            ImGui::BeginChild("##Breadcrumb", ImVec2(std::max(80.0f, ImGui::GetContentRegionAvail().x - rightReserve), ImGui::GetFrameHeight()),
                              ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (i > 0)
                {
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled(ICON_FA_CHEVRON_RIGHT);
                    ImGui::SameLine();
                }
                std::string name = (i == 0 ? std::string(ICON_FA_HOUSE " ") : std::string()) + parts[i].filename().string();
                ImGui::PushID((int)i);
                if (ImGui::Button(name.c_str()))
                    NavigateTo(parts[i]);
                // Konum çubuğuna bırakılan dosyayı o klasöre taşı
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                        MoveInto(fs::path((const char*)payload->Data), parts[i]);
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
            }
            // En sağ (son klasör) görünür kalsın
            ImGui::SetScrollHereX(1.0f);
            ImGui::EndChild();
        }

        // Sağ: arama, görünüm, boyut
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##AssetSearch", ICON_FA_MAGNIFYING_GLASS "  Search assets", m_Search, sizeof(m_Search));
        ImGui::SameLine();
        if (iconButton(m_ListView ? ICON_FA_TABLE_CELLS_LARGE : ICON_FA_LIST, m_ListView ? "Grid view" : "List view"))
            m_ListView = !m_ListView;
        if (!m_ListView)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("##ThumbSize", &m_ThumbnailSize, 48.0f, 160.0f, "");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Thumbnail size");
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void ContentBrowserPanel::DrawFolderTree(const fs::path& path, int depth)
    {
        std::error_code ec;
        if (!fs::is_directory(path, ec) || depth > 32)
            return;

        // Alt klasör var mı?
        std::vector<fs::path> subdirs;
        for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code ec2;
            if (it->is_directory(ec2) && !IsHidden(it->path()))
                subdirs.push_back(it->path());
        }
        std::sort(subdirs.begin(), subdirs.end(), [](const fs::path& a, const fs::path& b) {
            return ToLower(a.filename().string()) < ToLower(b.filename().string());
        });
        // Proje Packages klasörü: motor paketleri de burada listelenir (Unity'nin yerleşik paketleri gibi, kopyalanmaz)
        if (depth == 0 && path == m_BaseDirectory / "Packages")
            for (fs::directory_iterator it(EnginePackagesDir(), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
                if (std::error_code ec2; it->is_directory(ec2))
                    subdirs.push_back(it->path());

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_FramePadding;
        if (path == m_CurrentDirectory)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (subdirs.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (depth == 0)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        // Geçerli klasörün ataları açık olsun
        std::string cur = m_CurrentDirectory.string(), self = path.string();
        if (cur.size() > self.size() && cur.compare(0, self.size(), self) == 0)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        bool opened = ImGui::TreeNodeEx(self.c_str(), flags, "%s", "");
        const ImVec2 min = ImGui::GetItemRectMin();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            NavigateTo(path);
        const bool readOnly = IsReadOnly(path);
        if (!readOnly && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                MoveInto(fs::path((const char*)payload->Data), path);
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                CreatePrefabFrom(*(const Entity*)payload->Data, path);
            ImGui::EndDragDropTarget();
        }
        if (readOnly && path.parent_path() == EnginePackagesDir() && ImGui::IsItemHovered())
            ImGui::SetTooltip("Built-in package (read-only)");

        // İkon + ad
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
        float y = min.y + ImGui::GetStyle().FramePadding.y;
        const bool packageRoot = readOnly && path.parent_path() == EnginePackagesDir();
        const char* icon = depth == 0 ? (path.filename() == "Packages" ? ICON_FA_BOX_OPEN : ICON_FA_FOLDER_TREE)
                         : packageRoot ? ICON_FA_BOX
                                       : (opened && !subdirs.empty() ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER);
        dl->AddText(ImVec2(x, y), packageRoot ? IM_COL32(150, 190, 240, 255) : IM_COL32(232, 184, 76, 255), icon);
        const std::string label = packageRoot ? "Conqueror " + path.filename().string() : path.filename().string();
        dl->AddText(ImVec2(x + ImGui::GetFontSize() * 1.45f, y), ImGui::GetColorU32(readOnly ? ImGuiCol_TextDisabled : ImGuiCol_Text), label.c_str());

        if (opened)
        {
            for (auto& sub : subdirs)
                DrawFolderTree(sub, depth + 1);
            ImGui::TreePop();
        }
    }

    void ContentBrowserPanel::DrawContent()
    {
        m_HoveredFolder.clear();
        // Arama: tüm alt klasörlerde ara
        std::vector<Entry> searchResults;
        const bool searching = m_Search[0] != '\0';
        const std::vector<Entry>* entries = &m_Entries;
        // Find References / Select Dependencies sonuçları (Unity'de Project penceresi süzgeci)
        std::vector<Entry> resultEntries;
        if (!m_ResultsTitle.empty())
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
            ImGui::TextColored(UI::Colors::Accent, ICON_FA_MAGNIFYING_GLASS);
            ImGui::SameLine();
            ImGui::Text("%s  (%d)", m_ResultsTitle.c_str(), (int)m_Results.size());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK " Clear##results"))
            {
                m_ResultsTitle.clear();
                m_Results.clear();
            }
            ImGui::PopStyleVar();
            ImGui::Separator();
            std::error_code rec;
            for (const auto& p : m_Results)
                if (fs::exists(p, rec))
                    resultEntries.push_back(MakeEntry(p));
            if (!m_ResultsTitle.empty())
                entries = &resultEntries;
        }
        if (searching && m_ResultsTitle.empty())
        {
            std::string filter = ToLower(m_Search);
            std::error_code ec;
            int count = 0;
            for (fs::recursive_directory_iterator it(m_BaseDirectory / "Assets", fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end && count < 500; it.increment(ec))
            {
                if (IsHidden(it->path()))
                {
                    std::error_code ec2;
                    if (it->is_directory(ec2))
                        it.disable_recursion_pending();
                    continue;
                }
                std::string name = ToLower(it->path().filename().string());
                if (name.find(filter) == std::string::npos)
                    continue;
                Entry e;
                std::error_code ec2;
                e.Path = it->path();
                e.IsDirectory = it->is_directory(ec2);
                e.Name = e.IsDirectory ? e.Path.filename().string() : e.Path.stem().string();
                e.Extension = ToLower(e.Path.extension().string());
                if (!e.IsDirectory) e.Size = it->file_size(ec2);
                e.WriteTime = it->last_write_time(ec2);
                searchResults.push_back(std::move(e));
                ++count;
            }
            entries = &searchResults;
        }

        if (entries->empty())
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* msg = !m_ResultsTitle.empty() ? "Nothing found"
                               : searching             ? "No assets match the search"
                                                       : "This folder is empty  -  drop files here or use the + button";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPos(ImVec2(std::max(8.0f, (avail.x - ts.x) * 0.5f), std::max(8.0f, avail.y * 0.4f)));
            ImGui::TextDisabled("%s", msg);
        }
        else if (m_ListView)
        {
            if (ImGui::BeginTable("##AssetList", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableHeadersRow();
                for (const auto& e : *entries)
                    DrawListItem(e);
                ImGui::EndTable();
            }
        }
        else
        {
            const float thumb = m_ThumbnailSize;
            const float cellW = thumb + 18.0f;
            const int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));
            ImGui::Dummy(ImVec2(0, 2.0f));
            int col = 0;
            for (const auto& e : *entries)
            {
                if (col > 0)
                    ImGui::SameLine(0.0f, 0.0f);
                DrawGridItem(e, thumb);
                col = (col + 1) % columns;
            }
            ImGui::PopStyleVar();
        }

        // Boş alan: seçimi kaldır, bağlam menüsü, dışarıdan bırakma
        ImVec2 rest = ImGui::GetContentRegionAvail();
        if (rest.y > 4.0f)
        {
            ImGui::InvisibleButton("##EmptyArea", ImVec2(std::max(rest.x, 1.0f), rest.y));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                m_SelectedPath.clear();
            // Boş alana sağ tık: klasör menüsü (görünmez düğme pencere menüsünü engellemesin)
            ImGui::OpenPopupOnItemClick("##ContentBackground", ImGuiPopupFlags_MouseButtonRight);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    MoveInto(fs::path((const char*)payload->Data), m_CurrentDirectory);
                // Hiyerarşiden nesne bırak = prefab oluştur
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                    CreatePrefabFrom(*(const Entity*)payload->Data, m_CurrentDirectory);
                ImGui::EndDragDropTarget();
            }
        }
        DrawBackgroundContextMenu();
    }

    void ContentBrowserPanel::DrawFolderGlyph(ImDrawList* dl, const ImVec2& min, float size, bool open, bool hasContent)
    {
        const float pad = size * 0.10f;
        const float w = size - pad * 2.0f;
        const float h = w * 0.78f;
        const ImVec2 o(min.x + pad, min.y + (size - h) * 0.5f);
        const float r = size * 0.06f;
        const ImU32 back = IM_COL32(196, 146, 52, 255);
        const ImU32 front = open ? IM_COL32(247, 205, 102, 255) : IM_COL32(236, 186, 78, 255);
        const ImU32 shade = IM_COL32(0, 0, 0, 40);

        // Arka gövde + sekme
        dl->AddRectFilled(ImVec2(o.x, o.y), ImVec2(o.x + w * 0.42f, o.y + h * 0.22f), back, r, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(ImVec2(o.x, o.y + h * 0.12f), ImVec2(o.x + w, o.y + h), back, r);
        // İçerik kağıdı
        if (hasContent)
            dl->AddRectFilled(ImVec2(o.x + w * 0.08f, o.y + h * 0.18f), ImVec2(o.x + w * 0.92f, o.y + h * 0.8f), IM_COL32(245, 245, 240, 255), r * 0.5f);
        // Ön kapak
        dl->AddRectFilled(ImVec2(o.x, o.y + h * 0.30f), ImVec2(o.x + w, o.y + h), front, r);
        dl->AddLine(ImVec2(o.x + r, o.y + h * 0.30f + 1.0f), ImVec2(o.x + w - r, o.y + h * 0.30f + 1.0f), IM_COL32(255, 235, 170, 180), 1.0f);
        dl->AddRectFilled(ImVec2(o.x, o.y + h * 0.88f), ImVec2(o.x + w, o.y + h), shade, r, ImDrawFlags_RoundCornersBottom);
    }

    void ContentBrowserPanel::DrawFileGlyph(ImDrawList* dl, const ImVec2& min, float size, const Entry& entry)
    {
        AssetTypeInfo info = GetAssetTypeInfo(entry.Path, false);

        // Resim önizlemesi
        if (auto tex = GetThumbnail(entry))
        {
            float tw = (float)tex->GetWidth(), th = (float)tex->GetHeight();
            float scale = std::min((size - 6.0f) / tw, (size - 6.0f) / th);
            ImVec2 sz(tw * scale, th * scale);
            ImVec2 p(min.x + (size - sz.x) * 0.5f, min.y + (size - sz.y) * 0.5f);
            // Saydamlık için dama deseni
            const float cell = std::max(4.0f, size / 12.0f);
            dl->PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), true);
            for (float yy = p.y; yy < p.y + sz.y; yy += cell)
                for (float xx = p.x; xx < p.x + sz.x; xx += cell)
                {
                    bool dark = ((int)((xx - p.x) / cell) + (int)((yy - p.y) / cell)) & 1;
                    dl->AddRectFilled(ImVec2(xx, yy), ImVec2(xx + cell, yy + cell), dark ? IM_COL32(60, 60, 64, 255) : IM_COL32(85, 85, 90, 255));
                }
            dl->PopClipRect();
            dl->AddImage((ImTextureID)(uint64_t)tex->GetRendererID(), p, ImVec2(p.x + sz.x, p.y + sz.y), ImVec2(0, 1), ImVec2(1, 0));
            dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(0, 0, 0, 120));
            return;
        }

        // Belge şekli (kıvrık köşe) + tür rengi bandı + ikon
        const float w = size * 0.70f, h = size * 0.86f;
        const ImVec2 o(min.x + (size - w) * 0.5f, min.y + (size - h) * 0.5f);
        const float fold = w * 0.26f;
        const ImU32 paper = IM_COL32(58, 61, 68, 255);
        const ImU32 edge = IM_COL32(90, 94, 104, 255);
        ImVec2 pts[5] = { o, ImVec2(o.x + w - fold, o.y), ImVec2(o.x + w, o.y + fold), ImVec2(o.x + w, o.y + h), ImVec2(o.x, o.y + h) };
        dl->AddConvexPolyFilled(pts, 5, paper);
        dl->AddPolyline(pts, 5, edge, ImDrawFlags_Closed, 1.0f);
        ImVec2 foldPts[3] = { ImVec2(o.x + w - fold, o.y), ImVec2(o.x + w - fold, o.y + fold), ImVec2(o.x + w, o.y + fold) };
        dl->AddTriangleFilled(foldPts[0], foldPts[1], foldPts[2], IM_COL32(85, 89, 99, 255));

        // Renkli bant ve uzantı
        const float bandH = h * 0.22f;
        dl->AddRectFilled(ImVec2(o.x, o.y + h - bandH), ImVec2(o.x + w, o.y + h), info.Color);
        std::string ext = entry.Extension.empty() ? std::string("FILE") : entry.Extension.substr(1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::toupper(c); });
        if (ext.size() > 7) ext = ext.substr(0, 7);
        ImFont* font = ImGui::GetFont();
        const float extSize = std::max(8.0f, std::min(bandH * 0.62f, w / (std::max<size_t>(ext.size(), 4) * 0.62f)));
        ImVec2 es = font->CalcTextSizeA(extSize, FLT_MAX, 0.0f, ext.c_str());
        dl->AddText(font, extSize, ImVec2(o.x + (w - es.x) * 0.5f, o.y + h - bandH + (bandH - es.y) * 0.5f), IM_COL32(20, 20, 24, 255), ext.c_str());

        // Orta ikon (dinamik font boyutu ile keskin)
        const float iconSize = size * 0.30f;
        ImVec2 is = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, info.Icon);
        dl->AddText(font, iconSize, ImVec2(o.x + (w - is.x) * 0.5f, o.y + (h - bandH - is.y) * 0.5f + fold * 0.2f), info.Color, info.Icon);
    }

    void ContentBrowserPanel::DrawGridItem(const Entry& entry, float thumb)
    {
        const float cellW = thumb + 18.0f;
        const float textH = ImGui::GetTextLineHeight();
        const ImVec2 cellSize(cellW, thumb + textH + 14.0f);
        const bool selected = m_SelectedPath == entry.Path;
        const bool renaming = m_RenamingPath == entry.Path;

        ImGui::PushID(entry.Path.string().c_str());
        const ImVec2 cellMin = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();
        // Kimlik dosya adını taşır (otomasyon testleri varlığı adıyla bulur); PushID(yol) içinde tekil
        ImGui::InvisibleButton(("##cell_" + entry.Path.filename().string()).c_str(), cellSize,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (selected && m_ScrollToSelected)
        {
            ImGui::SetScrollHereY(0.5f);
            m_ScrollToSelected = false;
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && entry.IsDirectory)
            m_HoveredFolder = entry.Path;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 cardMin(cellMin.x + 4.0f, cellMin.y + 2.0f);
        const ImVec2 cardMax(cellMin.x + cellW - 4.0f, cellMin.y + cellSize.y - 2.0f);
        if (selected)
            dl->AddRectFilled(cardMin, cardMax, ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.30f)), 6.0f);
        else if (hovered)
            dl->AddRectFilled(cardMin, cardMax, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f)), 6.0f);
        if (selected)
            dl->AddRect(cardMin, cardMax, ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.8f)), 6.0f, 0, 1.2f);

        const ImVec2 thumbMin(cellMin.x + 9.0f, cellMin.y + 6.0f);
        if (entry.IsDirectory)
        {
            std::error_code ec;
            bool hasContent = !fs::is_empty(entry.Path, ec);
            DrawFolderGlyph(dl, thumbMin, thumb, hovered, hasContent);
        }
        else
        {
            DrawFileGlyph(dl, thumbMin, thumb, entry);
        }

        HandleItemInteraction(entry);

        // Ad / yeniden adlandırma
        const ImVec2 textPos(cellMin.x + 6.0f, thumbMin.y + thumb + 4.0f);
        if (renaming)
        {
            ImGui::SetCursorScreenPos(ImVec2(textPos.x, textPos.y - 3.0f));
            ImGui::SetNextItemWidth(cellW - 12.0f);
            if (m_RenameFrame == 0)
                ImGui::SetKeyboardFocusHere();
            bool done = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                m_RenamingPath.clear();
            else if (done || (ImGui::IsItemDeactivated() && m_RenameFrame > 1))
                CommitRename();
            m_RenameFrame++;
        }
        else
        {
            std::string label = Ellipsize(entry.Name, cellW - 12.0f);
            float tw = ImGui::CalcTextSize(label.c_str()).x;
            dl->AddText(ImVec2(cellMin.x + (cellW - tw) * 0.5f, textPos.y), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        }

        // Kart sonrası imleci hücre sonuna koy (sonraki SameLine için)
        ImGui::SetCursorScreenPos(ImVec2(cellMin.x, cellMin.y));
        ImGui::Dummy(cellSize);
        ImGui::PopID();
    }

    void ContentBrowserPanel::DrawListItem(const Entry& entry)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(entry.Path.string().c_str());

        AssetTypeInfo info = GetAssetTypeInfo(entry.Path, entry.IsDirectory);
        const bool selected = m_SelectedPath == entry.Path;
        if (m_RenamingPath == entry.Path)
        {
            ImGui::SetNextItemWidth(-1.0f);
            if (m_RenameFrame == 0)
                ImGui::SetKeyboardFocusHere();
            bool done = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                m_RenamingPath.clear();
            else if (done || (ImGui::IsItemDeactivated() && m_RenameFrame > 1))
                CommitRename();
            m_RenameFrame++;
        }
        else
        {
            std::string label = std::string("      ") + entry.Path.filename().string();
            ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            ImVec2 min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 2.0f, min.y), info.Color, info.Icon);
            HandleItemInteraction(entry);
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", info.Label);
        ImGui::TableSetColumnIndex(2);
        if (!entry.IsDirectory)
            ImGui::TextDisabled("%s", FormatSize(entry.Size).c_str());
        ImGui::TableSetColumnIndex(3);
        {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                entry.WriteTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            std::time_t t = std::chrono::system_clock::to_time_t(sctp);
            char buf[32] = {};
            if (std::tm* tm = std::localtime(&t))
                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
            ImGui::TextDisabled("%s", buf);
        }
        ImGui::PopID();
    }

    void ContentBrowserPanel::HandleItemInteraction(const Entry& entry)
    {
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemHovered() && entry.IsDirectory)
            m_HoveredFolder = entry.Path;

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (m_RenamingPath != entry.Path)
                m_RenamingPath.clear();
            m_SelectedPath = entry.Path;
            // Inspector dosyayı fare BIRAKILINCA gösterir, sürükleme başladıysa göstermez (Unity): seçili nesnenin bir
            // yuvasına sürüklerken Inspector o nesnede kalır
            if (!entry.IsDirectory && ImGui::IsItemClicked(ImGuiMouseButton_Left))
                m_PendingInspectorAsset = entry.Path;
        }
        if (!m_PendingInspectorAsset.empty() && m_PendingInspectorAsset == entry.Path && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (hovered)
                EditorState::Get().SelectAsset(entry.Path.string());
            m_PendingInspectorAsset.clear();
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (entry.IsDirectory)
                NavigateTo(entry.Path);
            else
                OpenFile(entry.Path);
        }

        if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            AssetTypeInfo info = GetAssetTypeInfo(entry.Path, entry.IsDirectory);
            if (ImGui::BeginItemTooltip())
            {
                ImGui::TextUnformatted(entry.Path.filename().string().c_str());
                ImGui::TextDisabled("%s%s%s", info.Label, entry.IsDirectory ? "" : "  |  ", entry.IsDirectory ? "" : FormatSize(entry.Size).c_str());
                if (auto it = m_Thumbnails.find(entry.Path.string()); it != m_Thumbnails.end() && it->second.Texture && !it->second.Failed)
                    ImGui::TextDisabled("%u x %u", it->second.Texture->GetWidth(), it->second.Texture->GetHeight());
                ImGui::EndTooltip();
            }
        }

        // Sürükle: diğer panellere (Inspector, Viewport, Lighting) ve klasörlere
        if (ImGui::BeginDragDropSource())
        {
            if (m_PendingInspectorAsset == entry.Path)
                m_PendingInspectorAsset.clear();   // sürükleme: seçim Inspector'a geçmez
            std::string itemPath = entry.Path.string();
            ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
            AssetTypeInfo info = GetAssetTypeInfo(entry.Path, entry.IsDirectory);
            ImGui::TextColored(ImColor(info.Color).Value, "%s", info.Icon);
            ImGui::SameLine();
            ImGui::TextUnformatted(entry.Path.filename().string().c_str());
            ImGui::EndDragDropSource();
        }
        // Klasöre bırak = taşı
        if (entry.IsDirectory && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                MoveInto(fs::path((const char*)payload->Data), entry.Path);
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                CreatePrefabFrom(*(const Entity*)payload->Data, entry.Path);
            ImGui::EndDragDropTarget();
        }

        DrawItemContextMenu(entry);
    }

    // Sağ tık menüleri ve Create menüsü: ContentBrowserMenus.cpp

    void ContentBrowserPanel::DrawModals()
    {
        if (m_OpenDeleteModal)
        {
            ImGui::OpenPopup("Delete Asset?");
            m_OpenDeleteModal = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete Asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            std::error_code ec;
            bool dir = fs::is_directory(m_PendingDelete, ec);
            ImGui::Text("%s  Delete \"%s\"%s?", ICON_FA_TRIANGLE_EXCLAMATION, m_PendingDelete.filename().string().c_str(),
                        dir ? " and everything inside it" : "");
            ImGui::TextDisabled("This cannot be undone.");
            ImGui::Spacing();
            if (UI::DangerButton("Delete", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
            {
                DeletePath(m_PendingDelete);
                m_PendingDelete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                m_PendingDelete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserPanel::DrawStatusBar()
    {
        ImGui::Separator();
        ImGui::AlignTextToFramePadding();
        if (!m_SelectedPath.empty())
        {
            std::error_code ec;
            bool dir = fs::is_directory(m_SelectedPath, ec);
            AssetTypeInfo info = GetAssetTypeInfo(m_SelectedPath, dir);
            ImGui::TextColored(ImColor(info.Color).Value, "%s", info.Icon);
            ImGui::SameLine();
            auto rel = fs::relative(m_SelectedPath, m_BaseDirectory, ec);
            ImGui::TextUnformatted((ec ? m_SelectedPath : rel).string().c_str());
            if (!dir)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("  %s  |  %s", info.Label, FormatSize(fs::file_size(m_SelectedPath, ec)).c_str());
            }
        }
        else
        {
            ImGui::TextDisabled("%d item%s", (int)m_Entries.size(), m_Entries.size() == 1 ? "" : "s");
        }
    }

    void ContentBrowserPanel::HandleKeyboard()
    {
        if (ImGui::GetIO().WantTextInput)
            return;
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        const bool alt = ImGui::GetIO().KeyAlt;
        // Unity Project penceresi kısayolları
        if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            m_Thumbnails.clear();
            m_Dirty = true;
        }
        if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_V, false) && HasClipboard())
            PasteInto(m_CurrentDirectory);
        if (!m_SelectedPath.empty())
        {
            if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_C, false))
                CopyAssets({ m_SelectedPath }, false);
            else if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_X, false) && !IsReadOnly(m_SelectedPath))
                CopyAssets({ m_SelectedPath }, true);
            else if (ctrl && alt && ImGui::IsKeyPressed(ImGuiKey_C, false))
            {
                std::error_code rec;
                ImGui::SetClipboardText(fs::relative(m_SelectedPath, m_BaseDirectory, rec).generic_string().c_str());
            }
            else if (alt && !ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
                OpenProperties(m_SelectedPath);
        }
        if (!m_SelectedPath.empty())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
                BeginRename(m_SelectedPath);
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !IsReadOnly(m_SelectedPath))
            {
                m_PendingDelete = m_SelectedPath;
                m_OpenDeleteModal = true;
            }
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
                DuplicatePath(m_SelectedPath);
            else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            {
                std::error_code ec;
                if (fs::is_directory(m_SelectedPath, ec)) NavigateTo(m_SelectedPath);
                else OpenFile(m_SelectedPath);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !m_BackStack.empty())
        {
            m_ForwardStack.push_back(m_CurrentDirectory);
            fs::path target = m_BackStack.back();
            m_BackStack.pop_back();
            NavigateTo(target, false);
        }
    }

    // ------------------------------------------------------------------ Dosya işlemleri

    void ContentBrowserPanel::SetSearch(const std::string& query)
    {
        std::snprintf(m_Search, sizeof(m_Search), "%s", query.c_str());
    }

    fs::path ContentBrowserPanel::MakeUniquePath(const fs::path& desired) const
    {
        std::error_code ec;
        if (!fs::exists(desired, ec))
            return desired;
        const std::string stem = desired.stem().string();
        const std::string ext = desired.extension().string();
        for (int i = 1; i < 10000; ++i)
        {
            fs::path candidate = desired.parent_path() / (stem + " " + std::to_string(i) + ext);
            if (!fs::exists(candidate, ec))
                return candidate;
        }
        return desired;
    }

    // Varlık taşındı / yeniden adlandırıldı: projedeki sahne, prefab ve malzeme dosyalarında eski yolu yenisiyle değiştir
    // (Unity bunu GUID ile yapar; bizde başvurular proje göreli yol). Açık sahnedeki nesneler de güncellenir.
    static void FixAssetReferences(const fs::path& source, const fs::path& target)
    {
        MaterialAsset::OnFileMoved(source.string(), target.string());
        if (EditorState::Get().GetSelectedAsset() == source.string())
            EditorState::Get().SelectAsset(target.string());
        if (!Project::GetActive())
            return;
        const fs::path projectDir = Project::GetActiveProjectDirectory();
        std::error_code ec;
        const std::string oldRel = fs::relative(source, projectDir, ec).generic_string();
        const std::string newRel = fs::relative(target, projectDir, ec).generic_string();
        if (oldRel.empty() || newRel.empty() || oldRel.rfind("..", 0) == 0)
            return;
        static const char* exts[] = { ".cqscene", ".cqprefab", ".cqmat", ".cqtimeline", ".cqcont", ".cqscenetemplate", ".cqtile", ".cqpalette", ".cqspriteatlas", ".cqvolume", ".cqlighting", ".cqflare", ".cqcrt", ".cqshadervariants", ".cqaudiorandom", ".cqmixer", ".cqterrainlayer", ".cqbrush", ".cqsignal", ".cqdata" };
        int files = 0;
        for (fs::recursive_directory_iterator it(Project::GetAssetDirectory(), fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec))
        {
            if (ec)
                break;
            if (!it->is_regular_file(ec))
                continue;
            const std::string ext = ToLower(it->path().extension().string());
            if (std::find_if(std::begin(exts), std::end(exts), [&](const char* e) { return ext == e; }) == std::end(exts) &&
                !SimpleAssets::ForExtension(ext) && !NodeAssetPanel::SchemaForExtension(ext))
                continue;
            std::ifstream in(it->path(), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            in.close();
            std::string text = ss.str();
            bool changed = false;
            // "Assets/Old.cqmat" (tam yol ya da klasör öneki "Assets/Old/")
            for (size_t pos = 0; (pos = text.find(oldRel, pos)) != std::string::npos;)
            {
                const size_t end = pos + oldRel.size();
                const bool startOk = pos == 0 || text[pos - 1] == ' ' || text[pos - 1] == '"' || text[pos - 1] == '\'' || text[pos - 1] == '/' || text[pos - 1] == ':';
                const bool endOk = end >= text.size() || text[end] == '\n' || text[end] == '\r' || text[end] == '"' || text[end] == '\'' || text[end] == '/';
                if (startOk && endOk)
                {
                    text.replace(pos, oldRel.size(), newRel);
                    pos += newRel.size();
                    changed = true;
                }
                else
                    pos = end;
            }
            if (changed)
            {
                std::ofstream(it->path(), std::ios::binary | std::ios::trunc) << text;
                ++files;
            }
        }
        if (files)
            CQ_CORE_INFO("Updated references in {0} file(s): {1} -> {2}", files, oldRel, newRel);
    }

    bool ContentBrowserPanel::MoveInto(const fs::path& source, const fs::path& directory)
    {
        std::error_code ec;
        if (source.empty() || !fs::exists(source, ec) || !fs::is_directory(directory, ec))
            return false;
        if (IsReadOnly(source) || IsReadOnly(directory))
        {
            CQ_CORE_WARN("Built-in packages are read-only");
            return false;
        }
        if (source.parent_path() == directory)
            return false;
        // Klasörü kendi içine taşıma
        std::string src = source.string(), dst = directory.string();
        if (dst.compare(0, src.size(), src) == 0 && (dst.size() == src.size() || dst[src.size()] == '/' || dst[src.size()] == '\\'))
        {
            CQ_CORE_WARN("Cannot move a folder into itself: {0}", src);
            return false;
        }
        fs::path target = MakeUniquePath(directory / source.filename());
        fs::rename(source, target, ec);
        if (ec)
        {
            // Farklı dosya sistemleri: kopyala + sil
            fs::copy(source, target, fs::copy_options::recursive, ec);
            if (!ec)
                fs::remove_all(source, ec);
        }
        if (ec)
        {
            CQ_CORE_ERROR("Move failed: {0} -> {1} ({2})", source.string(), target.string(), ec.message());
            return false;
        }
        CQ_CORE_INFO("Moved: {0} -> {1}", source.string(), target.string());
        // İçe aktarma ayarları dosyayla birlikte taşınır
        if (fs::exists(source.string() + ".meta", ec))
            fs::rename(source.string() + ".meta", target.string() + ".meta", ec);
        if (fs::exists(source.string() + ".cqsprite", ec))
            fs::rename(source.string() + ".cqsprite", target.string() + ".cqsprite", ec);
        // Sprite Atlas paketi ve içe aktarma ayarı
        if (fs::exists(source.string() + ".png", ec) && source.extension() == SpriteAtlases::Extension)
        {
            fs::rename(source.string() + ".png", target.string() + ".png", ec);
            fs::rename(source.string() + ".png.meta", target.string() + ".png.meta", ec);
            SpriteAtlases::Invalidate();
        }
        if (m_SelectedPath == source)
            m_SelectedPath = target;
        FixAssetReferences(source, target);
        m_Thumbnails.erase(source.string());
        m_Dirty = true;
        return true;
    }

    void ContentBrowserPanel::BeginRename(const fs::path& path)
    {
        if (IsReadOnly(path))
            return;
        m_RenamingPath = path;
        m_RenameFrame = 0;
        std::error_code ec;
        std::string name = fs::is_directory(path, ec) ? path.filename().string() : path.stem().string();
        snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name.c_str());
    }

    void ContentBrowserPanel::CommitRename()
    {
        fs::path source = m_RenamingPath;
        m_RenamingPath.clear();
        std::string newName = m_RenameBuffer;
        // Geçersiz karakterleri temizle
        newName.erase(std::remove_if(newName.begin(), newName.end(), [](char c) {
            return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
        }), newName.end());
        while (!newName.empty() && newName.back() == ' ') newName.pop_back();
        if (newName.empty() || source.empty())
            return;

        std::error_code ec;
        if (!fs::is_directory(source, ec))
            newName += source.extension().string();
        fs::path target = source.parent_path() / newName;
        if (target == source)
            return;
        if (fs::exists(target, ec))
        {
            CQ_CORE_WARN("An asset named '{0}' already exists", newName);
            return;
        }
        fs::rename(source, target, ec);
        if (ec)
        {
            CQ_CORE_ERROR("Rename failed: {0}", ec.message());
            return;
        }
        CQ_CORE_INFO("Renamed: {0} -> {1}", source.filename().string(), target.filename().string());
        if (fs::exists(source.string() + ".meta", ec))
            fs::rename(source.string() + ".meta", target.string() + ".meta", ec);
        if (fs::exists(source.string() + ".cqsprite", ec))
            fs::rename(source.string() + ".cqsprite", target.string() + ".cqsprite", ec);
        // Sprite Atlas paketi ve içe aktarma ayarı
        if (fs::exists(source.string() + ".png", ec) && source.extension() == SpriteAtlases::Extension)
        {
            fs::rename(source.string() + ".png", target.string() + ".png", ec);
            fs::rename(source.string() + ".png.meta", target.string() + ".png.meta", ec);
            SpriteAtlases::Invalidate();
        }
        // Yeni oluşturulmuş betik yeniden adlandırıldıysa içindeki sınıf adını da güncelle (C#/Java'da zorunlu)
        if (source == m_NewScriptPath && !m_NewScriptClass.empty())
        {
            std::string newClass = target.stem().string();
            newClass.erase(std::remove_if(newClass.begin(), newClass.end(), [](char c) { return !std::isalnum((unsigned char)c) && c != '_'; }), newClass.end());
            if (!newClass.empty() && !std::isdigit((unsigned char)newClass[0]))
            {
                std::ifstream in(target, std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                in.close();
                std::string text = ss.str(), out;
                const std::string& old = m_NewScriptClass;
                for (size_t i = 0; i < text.size();)
                {
                    bool boundaryBefore = i == 0 || !(std::isalnum((unsigned char)text[i - 1]) || text[i - 1] == '_');
                    size_t e = i + old.size();
                    bool boundaryAfter = e >= text.size() || !(std::isalnum((unsigned char)text[e]) || text[e] == '_');
                    if (boundaryBefore && text.compare(i, old.size(), old) == 0 && boundaryAfter)
                    {
                        out += newClass;
                        i = e;
                    }
                    else
                        out += text[i++];
                }
                std::ofstream(target, std::ios::binary) << out;
            }
        }
        m_NewScriptPath.clear();
        m_NewScriptClass.clear();
        FixAssetReferences(source, target);
        m_Thumbnails.erase(source.string());
        m_SelectedPath = target;
        m_Dirty = true;
    }

    void ContentBrowserPanel::DuplicatePath(const fs::path& path)
    {
        if (IsReadOnly(path))
            return;
        std::error_code ec;
        if (!fs::exists(path, ec))
            return;
        fs::path target = MakeUniquePath(path);
        if (target == path)
            target = MakeUniquePath(path.parent_path() / (path.stem().string() + " Copy" + path.extension().string()));
        // İçe aktarma ayarı / sprite kesimi de çoğaltılır (Unity Ctrl+D)
        if (!CopyWithSidecars(path, target))
            return;
        m_SelectedPath = target;
        m_Dirty = true;
    }

    void ContentBrowserPanel::DeletePath(const fs::path& path)
    {
        if (IsReadOnly(path))
            return;
        std::error_code ec;
        if (path.empty() || !fs::exists(path, ec))
            return;
        fs::remove_all(path, ec);
        if (ec)
        {
            CQ_CORE_ERROR("Delete failed: {0}", ec.message());
            return;
        }
        CQ_CORE_INFO("Deleted: {0}", path.string());
        fs::remove(path.string() + ".meta", ec);
        fs::remove(path.string() + ".cqsprite", ec);
        if (path.extension() == SpriteAtlases::Extension)
        {
            fs::remove(path.string() + ".png", ec);
            fs::remove(path.string() + ".png.meta", ec);
            SpriteAtlases::Invalidate();
        }
        if (EditorState::Get().GetSelectedAsset() == path.string())
            EditorState::Get().SelectAsset("");
        m_Thumbnails.erase(path.string());
        if (m_SelectedPath == path)
            m_SelectedPath.clear();
        // Silinen klasörün içindeysek üst klasöre çık
        std::string cur = m_CurrentDirectory.string(), del = path.string();
        if (cur.compare(0, del.size(), del) == 0)
            NavigateTo(path.parent_path(), false);
        m_Dirty = true;
    }

    fs::path ContentBrowserPanel::CreateAsset(const std::string& kind)
    {
        // Packages salt okunur (Unity gibi): orada açıkken oluşturma yapılmaz (menüler zaten kapalı; kısayollar da)
        if (IsReadOnly(m_CurrentDirectory))
        {
            CQ_CORE_WARN("Content Browser: Packages are read-only, nothing is created there");
            return {};
        }
        const fs::path dir = WritableDirectory();
        std::error_code ec;
        fs::path path;
        try
        {
            if (kind == "folder")
            {
                path = MakeUniquePath(dir / "New Folder");
                fs::create_directory(path, ec);
            }
            else if (kind == "scene")
            {
                path = MakeUniquePath(dir / "New Scene.cqscene");
                auto scene = std::make_shared<Scene>();
                SceneSerializer(scene).Serialize(path);
            }
            else if (kind.rfind("sprite:", 0) == 0)
            {
                const std::string spec = kind.substr(7);
                const std::string shape = spec.substr(0, spec.find('|'));
                const std::string display = spec.find('|') != std::string::npos ? spec.substr(spec.find('|') + 1) : shape;
                const fs::path src = EnginePackagesDir() / "Core" / "Sprites" / (shape + ".png");
                path = MakeUniquePath(dir / (display + ".png"));
                fs::copy_file(src, path, ec);
                if (ec)
                    path.clear();
                else
                {
                    TextureImportSettings settings = TextureImportSettings::DefaultsFor(TextureType::Sprite);
                    settings.PixelsPerUnit = 256.0f;   // 256 px = 1 birim (Unity hazır şekilleri gibi)
                    settings.Save(path.string());
                    if (shape == "NineSliced")
                    {
                        // 9-Sliced: köşeler (40 px) sabit kalır, Draw Mode Sliced / Tiled ile esner
                        SpriteSheet sheet;
                        sheet.TexturePath = path.string();
                        sheet.PixelsPerUnit = 256.0f;
                        SpriteDef def;
                        def.Name = path.stem().string();
                        def.Rect = { 0.0f, 0.0f, 256.0f, 256.0f };
                        def.Border = { 40.0f, 40.0f, 40.0f, 40.0f };
                        sheet.Sprites.push_back(def);
                        sheet.Save();
                    }
                }
            }
            else if (kind == "prefabvariant")
            {
                const fs::path basePath = m_SelectedPath;
                path = MakeUniquePath(dir / (basePath.stem().string() + " Variant" + PrefabSerializer::Extension));
                if (!PrefabSerializer::CreateVariant(basePath, path))
                    path.clear();
            }
            else if (kind == "scenetemplate")
            {
                // Seçili sahne varsa şablon onu kullanır (Unity: Scene Template From Scene)
                SceneTemplates::SceneTemplate t;
                const bool fromScene = m_SelectedPath.extension() == ".cqscene";
                path = MakeUniquePath(dir / ((fromScene ? m_SelectedPath.stem().string() : std::string("New Scene")) + " Template" +
                                             SceneTemplates::Extension));
                t.Name = path.stem().string();
                if (fromScene)
                    t.ScenePath = SceneTemplates::ToProjectRelative(m_SelectedPath);
                if (!SceneTemplates::Save(path, t))
                    path.clear();
            }
            else if (kind == "controller")
            {
                path = MakeUniquePath(dir / "New Animator Controller.cqcont");
                AnimationController::CreateDefault()->Serialize(path.string());
            }
            else if (kind == "blendspace2d" || kind == "blendspace1d")
            {
                const bool is2D = kind == "blendspace2d";
                path = MakeUniquePath(dir / (is2D ? "New Blend Space.cqblendspace" : "New Blend Space 1D.cqblendspace"));
                BlendSpaces::Save(path.string(), BlendSpaces::Default(is2D));
            }
            else if (kind == "overridecontroller")
            {
                path = MakeUniquePath(dir / "New Animator Override Controller.cqoverride");
                std::ofstream(path, std::ios::binary) << AnimatorOverrideInspector::DefaultContent();
            }
            else if (kind == "montage")
            {
                path = MakeUniquePath(dir / "New Montage.cqmontage");
                AnimMontage montage;
                montage.Name = path.stem().string();
                montage.ClipIndex = 0;
                montage.Sections.push_back({ "Default", 0.0f, "" });
                montage.Serialize(path.string());
            }
            else if (kind == "prefab")
            {
                // Boş prefab (tek kök nesne); sahneye sürüklenip doldurulur
                path = MakeUniquePath(dir / "New Prefab.cqprefab");
                auto scene = std::make_shared<Scene>();
                Entity root = scene->CreateEntity(path.stem().string());
                if (!PrefabSerializer::Serialize(root, path))
                    path.clear();
            }
            else if (kind == "material" || kind == "shadergraph")
            {
                path = MakeUniquePath(dir / (kind == "material" ? "New Material.cqmat" : "New Shader Graph.cqmat"));
                Material m;
                if (kind == "shadergraph")
                {
                    m.GraphNodes.emplace_back(1, ShaderGraphNodeType::MasterOutput, glm::vec2(420.0f, 120.0f));
                    m.NextGraphNodeID = 2;
                    m.GraphInitialized = true;
                }
                auto mat = MaterialAsset::Create(path.string(), &m);
                if (!mat)
                    path.clear();
                else if (kind == "shadergraph")
                {
                    mat->CompileShaderGraph();
                    MaterialEditorUI::OpenGraph(path.string());
                }
            }
            else if (kind == "materialvariant")
            {
                const fs::path parentPath = m_SelectedPath;
                auto parent = MaterialAsset::Load(parentPath.string());
                if (parent)
                {
                    path = MakeUniquePath(dir / (parentPath.stem().string() + " Variant.cqmat"));
                    Material m = *parent;
                    m.ParentPath = MaterialAsset::PathOf(parent.get());
                    if (!MaterialAsset::Create(path.string(), &m))
                        path.clear();
                }
            }
            else if (kind == "rendertexture")
            {
                path = MakeUniquePath(dir / "New Render Texture.cqrt");
                if (!RenderTargetRegistry::WriteAsset(path.string(), 512, 512))
                    path.clear();
            }
            else if (kind == "lightingsettings")
            {
                path = MakeUniquePath(dir / "New Lighting Settings.cqlighting");
                if (!SceneSerializer::SaveLightingSettings(path.string(), LightBakeSettings{}))
                    path.clear();
            }
            else if (kind == "lightmapparams")
            {
                path = MakeUniquePath(dir / (std::string("New Lightmap Parameters") + LightmapParameterAssets::Extension));
                if (!LightmapParameterAssets::Save(path.string(), LightmapParameters{}))
                    path.clear();
            }
            else if (kind == "customrendertexture")
            {
                path = MakeUniquePath(dir / (std::string("New Custom Render Texture") + CustomRenderTextures::Extension));
                if (!CustomRenderTextures::Save(path.string(), CustomRenderTextureAsset{}))
                    path.clear();
            }
            else if (kind == "lensflare")
            {
                path = MakeUniquePath(dir / (std::string("New Lens Flare") + LensFlareAssets::Extension));
                if (!LensFlareAssets::Save(path.string(), LensFlareAssets::DefaultElements()))
                    path.clear();
            }
            else if (kind == "volumeprofile")
            {
                path = MakeUniquePath(dir / (std::string("New Volume Profile") + VolumeProfiles::Extension));
                PostProcessVolumeComponent defaults;
                if (!VolumeProfiles::Save(path.string(), defaults))
                    path.clear();
            }
            else if (kind == "spriteatlas")
            {
                SpriteAtlasAsset atlas;
                // Seçili görsel / klasör pakete eklenir
                if (!m_SelectedPath.empty() && (fs::is_directory(m_SelectedPath, ec) || IsImage(ToLower(m_SelectedPath.extension().string()))) &&
                    m_SelectedPath != m_CurrentDirectory)
                    atlas.Objects.push_back(fs::relative(m_SelectedPath, m_BaseDirectory, ec).generic_string());
                path = MakeUniquePath(dir / (std::string("New Sprite Atlas") + SpriteAtlases::Extension));
                if (!SpriteAtlases::Save(path.string(), atlas))
                    path.clear();
            }
            else if (kind.rfind("tilepalette:", 0) == 0)
            {
                GridSystem::TilePaletteAsset palette;
                palette.Layout = (GridLayout)std::clamp(std::atoi(kind.c_str() + 12), 0, 3);
                path = MakeUniquePath(dir / (std::string("New Tile Palette") + GridSystem::PaletteExtension));
                if (!GridSystem::SavePalette(path.string(), palette))
                    path.clear();
            }
            else if (kind == "tile" || kind == "ruletile" || kind == "animatedtile")
            {
                TileDefinition def;
                def.Name = kind == "ruletile" ? "New Rule Tile" : (kind == "animatedtile" ? "New Animated Tile" : "New Tile");
                def.Color = glm::vec4(1.0f);
                // Seçili görsel varsa karo onu kullanır (Unity: sprite'tan Tile)
                if (IsImage(ToLower(m_SelectedPath.extension().string())))
                    def.Texture = fs::relative(m_SelectedPath, m_BaseDirectory, ec).generic_string();
                if (kind == "ruletile")
                    def.Rules.push_back({});
                if (kind == "animatedtile")
                {
                    def.FrameRate = 6.0f;
                    if (!def.Texture.empty())
                        def.Frames.push_back(def.Texture);
                }
                path = MakeUniquePath(dir / (def.Name + GridSystem::TileExtension));
                if (!GridSystem::SaveTileAsset(path.string(), def))
                    path.clear();
            }
            else if (kind == "physicsmaterial2d")
            {
                path = MakeUniquePath(dir / "New Physics Material 2D.cqphysmat2d");
                PhysicsMaterialAsset m;
                m.Friction = 0.4f;   // Unity PhysicsMaterial2D varsayılanı
                if (!PhysicsMaterials::Save(m, path.string()))
                    path.clear();
            }
            else if (kind == "physicsmaterial")
            {
                path = MakeUniquePath(dir / "New Physics Material.cqphysmat");
                if (!PhysicsMaterials::Save(PhysicsMaterialAsset{}, path.string()))
                    path.clear();
            }
            else if (kind == "timeline")
            {
                path = MakeUniquePath(dir / "New Timeline.cqtimeline");
                TimelineAsset timeline;
                if (!timeline.Save(path))
                    path.clear();
            }
            else if (kind == "testscript" || kind == "playablescript")
            {
                const bool test = kind == "testscript";
                path = MakeUniquePath(dir / (test ? "NewTests.cqs" : "NewPlayableBehaviour.cqs"));
                const std::string className = Visual::SanitizeIdentifier(path.stem().string());
                std::ofstream out(path, std::ios::binary);
                out << (test ? Scripting::TestRunner::NewScriptTemplate(className)
                             : "using nativebridge;\n\n// Add to a Playable track clip, directly or through a Playable Asset.\nscript " + className +
                                   " : ConquerorScript\n{\n    public float intensity = 1.0;\n\n    // The clip started\n    func OnClipStart() { }\n\n"
                                   "    // Every frame during the clip: time = seconds since it started, progress = 0..1\n    func OnClipUpdate(time, progress) { }\n\n"
                                   "    // The clip ended or the Timeline stopped\n    func OnClipStop() { }\n}\n");
                if (!out.good())
                    path.clear();
            }
            else if (kind == "materialfunction")
            {
                path = MakeUniquePath(dir / "New Material Function.cqmatfunc");
                Material m;
                m.GraphNodes.emplace_back(1, ShaderGraphNodeType::MasterOutput, glm::vec2(700.0f, 120.0f));
                m.GraphNodes.emplace_back(2, ShaderGraphNodeType::FunctionInput, glm::vec2(120.0f, 120.0f));
                m.GraphNodes.emplace_back(3, ShaderGraphNodeType::FunctionOutput, glm::vec2(460.0f, 120.0f));
                m.NextGraphNodeID = 4;
                m.GraphLinks.emplace_back(1, 2, 0, 3, 0);
                m.NextGraphLinkID = 2;
                m.GraphInitialized = true;
                auto mat = MaterialAsset::Create(path.string(), &m);
                if (!mat)
                    path.clear();
                else
                    MaterialEditorUI::OpenGraph(path.string());
            }
            else if (kind == "particleasset")
            {
                path = MakeUniquePath(dir / (std::string("New Particle System") + ParticleAssets::Extension));
                ParticleSystemComponent ps;
                if (!ParticleAssets::Save(path.string(), ps))
                    path.clear();
            }
            else if (kind == "datatable" || kind == "curvetable")
            {
                const bool curve = kind == "curvetable";
                path = MakeUniquePath(dir / (curve ? "New Curve Table.cqcurvetable" : "New Data Table.cqdatatable"));
                std::ofstream out(path, std::ios::binary);
                out << (curve ? "Name,0,1\nRow1,0,1\n" : "Name,Value\nRow1,0\n");
                if (!out.good())
                    path.clear();
            }
            else if (kind.rfind("scripttpl:", 0) == 0 || kind == "editorutility")
            {
                const std::string t = kind == "editorutility" ? "utility" : kind.substr(10);
                const std::string stem = t == "interface" ? "INewInterface" : t == "library" ? "NewLibrary" : t == "enum" ? "NewEnum" : t == "struct" ? "NewStruct" : "NewEditorUtility";
                path = MakeUniquePath(dir / (stem + ".cqs"));
                const std::string name = Visual::SanitizeIdentifier(path.stem().string());
                std::string text;
                if (t == "interface")
                    text = "// Implement it in a script: script Enemy : ConquerorScript, " + name + "\n// Call it on any object with: SendMessage(target, \"TakeDamage\", 10);\ninterface " + name + "\n{\n    func TakeDamage(amount);\n}\n";
                else if (t == "library")
                    text = "// Other scripts load it with: using " + name + ";\n// Then call: " + name + ".Double(3)\nclass " + name + "\n{\n    static func Double(value) { return value * 2; }\n}\n";
                else if (t == "enum")
                    text = "// Other scripts load it with: using " + name + ";\nenum " + name + "\n{\n    First,\n    Second\n}\n";
                else if (t == "struct")
                    text = "// Other scripts load it with: using " + name + ";\nstruct " + name + "\n{\n    public float value = 0.0;\n}\n";
                else
                    text = "using nativebridge;\n\n// Select this file and press Run in the Inspector.\nscript " + name + " : EditorUtility\n{\n    func Run()\n    {\n        print(\"Hello from the editor\");\n    }\n}\n";
                std::ofstream out(path, std::ios::binary);
                out << text;
                if (!out.good())
                    path.clear();
            }
            else if (kind == "eqs")
            {
                path = MakeUniquePath(dir / "New Environment Query.cqeqs");
                EnvQueryDesc desc;
                EnvTest test;
                test.Type = EnvTestType::Distance;
                test.Weight = -1.0f;
                desc.Tests.push_back(test);
                if (!EnvQuery::Save(path.string(), desc))
                    path.clear();
            }
            else if (kind == "uidocument" || kind == "uistyle")
            {
                const bool document = kind == "uidocument";
                path = MakeUniquePath(dir / (document ? std::string("NewUIDocument") + UIToolkit::DocumentExtension : std::string("NewStyleSheet") + UIToolkit::StyleExtension));
                std::ofstream out(path, std::ios::binary);
                out << (document ? UIToolkit::NewDocumentTemplate() : UIToolkit::NewStyleTemplate());
                if (!out.good())
                    path.clear();
            }
            else if (kind == "playableasset")
            {
                path = MakeUniquePath(dir / "New Playable Asset.cqplayable");
                DataAssets::DataAssetFile file;
                if (!DataAssets::Save(path.string(), file))
                    path.clear();
            }
            else if (kind.rfind("simple:", 0) == 0)
            {
                if (const SimpleAssets::Def* def = SimpleAssets::ForKind(kind.substr(7)))
                {
                    path = MakeUniquePath(dir / ("New " + def->Label + def->Extension));
                    SimpleAssets::Data data = SimpleAssets::Defaults(*def);
                    // Search: o anki arama metni kaydedilir
                    if (def->Kind == "SavedSearch")
                        data.Values["Query"] = m_Search;
                    if (!SimpleAssets::Save(path.string(), *def, data))
                        path.clear();
                }
            }
            else if (kind.rfind("nodeasset:", 0) == 0)
            {
                RegisterBuiltinNodeAssetSchemas();
                if (const NodeAssetSchema* schema = NodeAssetPanel::SchemaForKind(kind.substr(10)))
                {
                    path = MakeUniquePath(dir / ("New " + schema->Title + schema->Extension));
                    if (!NodeAssetPanel::CreateFile(*schema, path))
                        path.clear();
                }
            }
            else if (kind == "visualscript")
            {
                path = MakeUniquePath(dir / "NewVisualScript.cqgraph");
                Visual::Graph graph;
                graph.Name = path.stem().string();
                graph.AddNode("event.start", 80.0f, 80.0f);
                graph.AddNode("event.update", 80.0f, 260.0f);
                if (!graph.Save(path.string()))
                    path.clear();
            }
            else if (kind.rfind("dataasset:", 0) == 0)
            {
                const std::string spec = kind.substr(10);
                const size_t bar = spec.find('|');
                DataAssets::DataAssetFile data;
                data.Script = spec.substr(0, bar);
                data.Class = bar == std::string::npos ? std::string() : spec.substr(bar + 1);
                path = MakeUniquePath(dir / ("New " + data.Class + DataAssets::Extension));
                if (!DataAssets::Save(path.string(), data))
                    path.clear();
            }
            else if (kind == "dataassetscript" || kind.rfind("dataassetscript:", 0) == 0)
            {
                const std::string language = kind.size() > 16 ? kind.substr(16) : std::string("cqs");
                auto* runtime = Scripting::ScriptRuntimes::ForLanguage(language);
                if (runtime && DataAssets::SupportsDataClasses(language))
                {
                    path = MakeUniquePath(dir / ("NewDataAsset" + (language == "cpp" ? std::string(".h") : runtime->GetExtensions().front())));
                    std::string name = path.stem().string();
                    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return !std::isalnum((unsigned char)c) && c != '_'; }), name.end());
                    std::ofstream f(path);
                    f << DataAssets::NewScriptTemplate(name, language);
                    f.close();
                    m_NewScriptPath = path;   // adlandırınca sınıf adı da değişir
                    m_NewScriptClass = name;
                }
            }
            else if (kind == "animclip")
            {
                // Unity Create > Animation > Animation Clip (Animation penceresinde düzenlenir)
                PropertyClip clip;
                path = MakeUniquePath(dir / (std::string("New Animation") + PropertyClips::Extension));
                if (!PropertyClips::Save(path.string(), clip))
                    path.clear();
            }
            else if (kind == "avatarmask")
            {
                // Unity Create > Avatar Mask; seçili model varsa iskeleti kaynak olur
                AvatarMask mask;
                const std::string e = ToLower(m_SelectedPath.extension().string());
                if (e == ".fbx" || e == ".glb" || e == ".gltf" || e == ".dae")
                    mask.SourceModel = SceneSerializer::ToAssetPath(m_SelectedPath.string());
                path = MakeUniquePath(dir / (std::string("New Avatar Mask") + AvatarMasks::Extension));
                if (!AvatarMasks::Save(path.string(), mask))
                    path.clear();
            }
            else if (kind == "cubemap")
            {
                // Unity Create > Legacy > Cubemap; seçili resim varsa panorama olarak kullanılır
                CubemapAssetData data;
                if (IsImage(ToLower(m_SelectedPath.extension().string())))
                {
                    data.Source = CubemapAssetData::SourceKind::Panorama;
                    data.Panorama = SceneSerializer::ToAssetPath(m_SelectedPath.string());
                }
                path = MakeUniquePath(dir / (std::string("New Cubemap") + CubemapAssets::Extension));
                if (!CubemapAssets::Save(path.string(), data))
                    path.clear();
            }
            else if (kind.rfind("curve:", 0) == 0)
            {
                // Unreal Curve Float / Curve Vector / Curve Linear Color
                const std::string t = kind.substr(6);
                const CurveType type = t == "vector" ? CurveType::Vector : t == "color" ? CurveType::Color : CurveType::Float;
                const char* name = type == CurveType::Vector ? "New Vector Curve" : type == CurveType::Color ? "New Color Curve" : "New Float Curve";
                path = MakeUniquePath(dir / (std::string(name) + CurveAssets::Extension));
                if (!CurveAssets::Save(path.string(), CurveAsset::Default(type)))
                    path.clear();
            }
            else if (kind == "inputactions")
            {
                path = MakeUniquePath(dir / (std::string("New Input Actions") + InputActionAssets::Extension));
                if (!InputActionAssets::CreateDefault(path.string()))
                    path.clear();
            }
            else if (kind == "signal")
            {
                path = MakeUniquePath(dir / (std::string("New Signal") + SignalAssets::Extension));
                if (!SignalAssets::Create(path.string()))
                    path.clear();
            }
            else if (kind == "terrainlayer" || kind == "terrainbrush")
            {
                const bool image = IsImage(ToLower(m_SelectedPath.extension().string()));
                if (kind == "terrainlayer")
                {
                    TerrainLayer l;
                    if (image)
                        l.TexturePath = m_SelectedPath.string();
                    path = MakeUniquePath(dir / ((image ? m_SelectedPath.stem().string() : std::string("New Terrain Layer")) + TerrainAssets::LayerExtension));
                    if (!TerrainAssets::SaveLayer(path.string(), l))
                        path.clear();
                }
                else
                {
                    TerrainBrushAsset b;
                    if (image)
                        b.Mask = fs::relative(m_SelectedPath, m_BaseDirectory, ec).generic_string();
                    path = MakeUniquePath(dir / ((image ? m_SelectedPath.stem().string() : std::string("New Brush")) + TerrainAssets::BrushExtension));
                    if (!TerrainAssets::SaveBrush(path.string(), b))
                        path.clear();
                }
            }
            else if (kind == "audiorandom")
            {
                AudioRandomContainerAsset a;
                // Seçili ses dosyası başlangıç klibi olur
                const std::string ext = ToLower(m_SelectedPath.extension().string());
                if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
                    a.Clips.push_back(fs::relative(m_SelectedPath, m_BaseDirectory, ec).generic_string());
                path = MakeUniquePath(dir / (std::string("New Audio Random Container") + AudioRandomContainers::Extension));
                if (!AudioRandomContainers::Save(path.string(), a))
                    path.clear();
            }
            else if (kind == "audiomixer")
            {
                path = MakeUniquePath(dir / "New Audio Mixer.cqmixer");
                std::ofstream f(path);
                f << "AudioMixer: 1\nOutput: Master\nGroups:\n  - Name: Master\n    Parent: \"\"\n    Volume: 0\n";
                f.close();
                if (AudioMixer::Get().IsInitialized())
                    AudioMixer::Get().LoadAssetMixer(path);
            }
            else if (kind == "shadervariants")
            {
                path = MakeUniquePath(dir / (std::string("New Shader Variant Collection") + ShaderVariantCollections::Extension));
                if (!ShaderVariantCollections::Save(path.string(), {}))
                    path.clear();
            }
            else if (kind.rfind("shader:", 0) == 0)
            {
                const std::string t = kind.substr(7);
                const char* name = t == "lit" ? "New Lit Shader.cqsh" : t == "unlit" ? "New Unlit Shader.cqsh"
                                 : t == "imageeffect" ? "New Image Effect Shader.glsl" : t == "compute" ? "New Compute Shader.comp"
                                 : t == "raytracing" ? "New Ray Tracing Shader.cqraytrace"
                                 : "New Custom Render Texture Shader.glsl";
                path = MakeUniquePath(dir / name);
                std::ofstream f(path);
                f << (t == "lit" ? ShaderTemplates::Lit() : t == "unlit" ? ShaderTemplates::Unlit()
                      : t == "imageeffect" ? ShaderTemplates::ImageEffect() : t == "compute" ? ShaderTemplates::Compute()
                      : t == "raytracing" ? ShaderTemplates::RayTracing()
                      : ShaderTemplates::CustomRenderTexture());
            }
            else if (kind == "shader")
            {
                path = MakeUniquePath(dir / "NewShader.cqsh");
                std::ofstream f(path);
                f << "// Conqueror shader: custom shader with a PBR surface function\n"
                     "#type vertex\n#version 460 core\n"
                     "layout(location = 0) in vec3 a_Position;\n"
                     "layout(location = 1) in vec3 a_Normal;\n"
                     "layout(location = 2) in vec2 a_TexCoord;\n"
                     "#include \"Include/FrameData.glsl\"\n"
                     "uniform mat4 u_Transform;\n"
                     "out vec3 v_Normal;\nout vec2 v_UV;\n"
                     "void main()\n{\n"
                     "    v_Normal = mat3(transpose(inverse(u_Transform))) * a_Normal;\n"
                     "    v_UV = a_TexCoord;\n"
                     "    gl_Position = CQ_Frame.ViewProjection * u_Transform * vec4(a_Position, 1.0);\n"
                     "}\n\n"
                     "#type fragment\n#version 460 core\n"
                     "#include \"Include/FrameData.glsl\"\n"
                     "in vec3 v_Normal;\nin vec2 v_UV;\n"
                     "layout(location = 0) out vec4 o_Color;\n"
                     "uniform vec4 u_Color = vec4(1.0);\n"
                     "void main()\n{\n"
                     "    float ndl = max(dot(normalize(v_Normal), -normalize(CQ_Frame.SunDirection.xyz)), 0.0);\n"
                     "    o_Color = vec4(u_Color.rgb * (0.2 + 0.8 * ndl), u_Color.a);\n"
                     "}\n";
            }
            else if (kind.rfind("script:", 0) == 0)
            {
                auto* runtime = Scripting::ScriptRuntimes::ForLanguage(kind.substr(7));
                if (runtime)
                {
                    path = MakeUniquePath(dir / ("NewScript" + runtime->GetExtensions().front()));
                    std::string name = path.stem().string();
                    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return !std::isalnum((unsigned char)c) && c != '_'; }), name.end());
                    std::ofstream f(path);
                    f << runtime->GetNewScriptTemplate(name);
                    m_NewScriptPath = path;
                    m_NewScriptClass = name;
                }
            }
            else if (kind == "cqs")
            {
                path = MakeUniquePath(dir / "NewScript.cqs");
                std::string name = path.stem().string();
                name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
                std::ofstream f(path);
                f << "using nativebridge;\n\nscript " << name << " : ConquerorScript\n{\n"
                     "    // Public fields appear in the Inspector\n"
                     "    public float speed = 1.0;\n\n"
                     "    func Start()\n    {\n    }\n\n"
                     "    func Update(dt)\n    {\n    }\n}\n";
            }
            else if (kind == "cpp")
            {
                path = MakeUniquePath(dir / "NewBehaviour.cpp");
                std::string name = path.stem().string();
                name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
                std::ofstream f(path);
                // Native Script (motorun tam C++ API'si): Native Script bileşenine sürüklenir, motor Play'de derler
                f << "#include \"Scripting/ScriptableEntity.h\"\n#include \"Scene/Components.h\"\n\n"
                     "// Native Script: drag this file onto a Native Script component. The engine compiles it when you press Play\n"
                     "// (only when the file changed); builds ship the compiled library.\n"
                     "class " << name << " : public Conqueror::ScriptableEntity\n{\npublic:\n"
                     "    float Speed = 90.0f;   // degrees per second\n\n"
                     "    void OnCreate() override\n    {\n    }\n\n"
                     "    void OnUpdate(float deltaTime) override\n    {\n"
                     "        auto& transform = GetComponent<Conqueror::TransformComponent>();\n"
                     "        transform.Rotation.y += glm::radians(Speed) * deltaTime;\n    }\n};\n\n"
                     "CQ_NATIVE_SCRIPT(" << name << ")\n";
            }
            else if (kind == "text")
            {
                path = MakeUniquePath(dir / "New Text.txt");
                std::ofstream f(path);
            }
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Create asset failed: {0}", e.what());
            return {};
        }
        if (!path.empty())
            CQ_CORE_INFO("Created: {0}", path.string());
        return path;
    }

    void ContentBrowserPanel::CreatePrefabFrom(Entity entity, const fs::path& directory)
    {
        if (!entity || !entity.GetScene() || !entity.GetScene()->m_Registry.valid((entt::entity)entity))
            return;
        fs::path written = AssetActions::CreatePrefab(entity, directory);
        if (written.empty())
            return;
        RefreshEntries(true);
        m_SelectedPath = written;
        CQ_CORE_INFO("[Prefab] Created {0}", written.filename().string());
    }

    void ContentBrowserPanel::OpenFile(const fs::path& path)
    {
        std::error_code ec;
        if (!fs::exists(path, ec) || fs::is_directory(path, ec))
            return;

        const std::string ext = ToLower(path.extension().string());
        if (ext == ".cqmat" || ext == ".cqmatfunc")
        {
            if (!IsReadOnly(path))
                MaterialEditorUI::OpenGraph(path.string());   // Unreal: malzeme çift tıkla Material Editor'de açılır
            return;
        }
        // Grafik varlıkları kendi düzenleyicisinde açılır; Inspector'da düzenlenen varlıklar açılınca dosya yöneticisi çıkmaz
        if (NodeAssetPanel::SchemaForExtension(ext) || ext == ".cqsearch" || ext == ".cqeqs" || ext == UIToolkit::DocumentExtension || ext == UIToolkit::StyleExtension)
        {
            if (m_FileOpenCallback)
                m_FileOpenCallback(path);
            return;
        }
        if (SimpleAssets::ForExtension(ext) || ext == ".cqplayable" || ext == ParticleAssets::Extension || ext == ".cqdatatable" || ext == ".cqcurvetable")
            return;
        static const char* internal[] = { ".cpp", ".h", ".hpp", ".c", ".cs", ".cqs", ".cqsh", ".glsl", ".cqcont", ".cqscene",
                                          ".lua", ".luaq", ".luau", ".py", ".java", ".txt", ".md", ".json", ".yaml", ".yml", ".ini",
                                          ".cqprefab", ".cqgraph", ".cqmontage", ".cqinputactions", ".cqanim" };
        for (const char* e : internal)
        {
            if (ext == e && m_FileOpenCallback)
            {
                m_FileOpenCallback(path);
                return;
            }
        }
        RevealInFileManager(path);
    }

    void ContentBrowserPanel::RevealInFileManager(const fs::path& path)
    {
        std::string p = path.string();
#ifdef CQ_PLATFORM_WINDOWS
        std::string command = "start \"\" \"" + p + "\"";
#elif defined(CQ_PLATFORM_LINUX)
        std::string command = "xdg-open \"" + p + "\" >/dev/null 2>&1 &";
#else
        std::string command = "open \"" + p + "\"";
#endif
        int rc = std::system(command.c_str());
        (void)rc;
    }

    void ContentBrowserPanel::ImportDroppedFiles()
    {
        const fs::path target = (!m_HoveredFolder.empty() && !IsReadOnly(m_HoveredFolder)) ? m_HoveredFolder : WritableDirectory();
        for (const std::string& file : m_DroppedFiles)
        {
            const fs::path source(file);
            std::error_code ec;
            if (!fs::exists(source, ec))
            {
                CQ_CORE_WARN("Import skipped, not found: {0}", file);
                continue;
            }
            if (fs::weakly_canonical(source.parent_path(), ec) == fs::weakly_canonical(target, ec))
                continue;   // zaten bu klasörde
            const fs::path dest = MakeUniquePath(target / source.filename());
            fs::copy(source, dest, fs::copy_options::recursive, ec);
            if (ec)
            {
                CQ_CORE_ERROR("Import failed: {0} ({1})", file, ec.message());
                continue;
            }
            CQ_CORE_INFO("Imported: {0}", dest.string());
            m_SelectedPath = dest;
            m_ScrollToSelected = true;
        }
        m_DroppedFiles.clear();
        m_Dirty = true;
    }

    void ContentBrowserPanel::ImportFile()
    {
        const nfdpathset_t* paths = nullptr;
        nfdresult_t result = NFD_OpenDialogMultipleU8(&paths, nullptr, 0, nullptr);
        if (result != NFD_OKAY)
        {
            if (result == NFD_ERROR)
                CQ_CORE_ERROR("File dialog error: {0}", NFD_GetError());
            return;
        }

        nfdpathsetsize_t count = 0;
        NFD_PathSet_GetCount(paths, &count);
        for (nfdpathsetsize_t i = 0; i < count; ++i)
        {
            nfdu8char_t* outPath = nullptr;
            if (NFD_PathSet_GetPathU8(paths, i, &outPath) != NFD_OKAY)
                continue;
            fs::path source(outPath);
            NFD_PathSet_FreePathU8(outPath);
            fs::path dest = MakeUniquePath(WritableDirectory() / source.filename());
            std::error_code ec;
            fs::copy(source, dest, fs::copy_options::recursive, ec);
            if (ec)
                CQ_CORE_ERROR("Import failed: {0} ({1})", source.string(), ec.message());
            else
                CQ_CORE_INFO("Imported: {0}", dest.string());
        }
        NFD_PathSet_Free(paths);
        m_Dirty = true;
    }
}
