#include "Renderer/AntiAliasing.h"
#include "Renderer/RayTracing/RayTracedGI.h"
#include "Scene/MeshRendererMaterial.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "ObjectPicker.h"
#include <IconsFontAwesome6.h>
#include "LightingPanel.h"
#include "Scene/LightProbeGroup.h"
#include "UI/EditorUI.h"
#include "Renderer/RHI/Cubemap.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Serialization/SceneSerializer.h"
#include <filesystem>
#include "Core/Utils/FileSystem.h"
#include "Scene/Components.h"

#include <imgui.h>
#include <nfd.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace Conqueror::Editor
{
    namespace
    {
        std::filesystem::path EngineDirectory()
        {
            std::string exe = FileSystem::GetExecutableDirectory();
            return exe.empty() ? std::filesystem::current_path() : std::filesystem::path(exe);
        }

        std::string NowString()
        {
            std::time_t t = std::time(nullptr);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
            return buf;
        }

        void HashCombine(uint64_t& h, uint64_t v)
        {
            h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        }

        void HashFloat(uint64_t& h, float f)
        {
            uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            HashCombine(h, bits);
        }

        void HashVec3(uint64_t& h, const glm::vec3& v)
        {
            HashFloat(h, v.x);
            HashFloat(h, v.y);
            HashFloat(h, v.z);
        }

        const char* LightModeName(LightMode mode)
        {
            switch (mode)
            {
                case LightMode::Realtime: return "Realtime";
                case LightMode::Mixed:    return "Mixed";
                case LightMode::Baked:    return "Baked";
            }
            return "?";
        }
    }

    LightingPanel::~LightingPanel()
    {
        CancelBake();
        JoinWorker();
    }

    void LightingPanel::SetContext(std::shared_ptr<Scene> scene)
    {
        if (scene == m_Context)
            return;
        // Sahne değişti: kayıtlı sonuç bilgileri artık geçersiz
        m_Context = scene;
        m_BakedInfo = {};
        m_RealtimeInfo = {};
        m_APVInfo = {};
        m_LastLightingHash = 0;
        m_AutoPending = false;
        if (m_Context && m_Context->GetBakedLightmapTexture())
        {
            auto tex = m_Context->GetBakedLightmapTexture();
            m_BakedInfo.Valid = true;
            m_BakedInfo.AtlasWidth = (int)tex->GetWidth();
            m_BakedInfo.AtlasHeight = (int)tex->GetHeight();
            m_BakedInfo.When = "loaded from disk";
        }
    }

    // ===================================================================== Ana pencere

    void LightingPanel::OnImGuiRender(bool* open)
    {
        PollBake();
        UpdateAutoGenerate();

        ImGui::SetNextWindowSize(ImVec2(460, 720), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Lighting", open))
        {
            ImGui::End();
            return;
        }

        if (!m_Context)
        {
            UI::InfoBox("No active scene.", UI::InfoType::Warning);
            ImGui::End();
            return;
        }

        DrawToolbar();

        if (ImGui::BeginTabBar("##LightingTabs", ImGuiTabBarFlags_FittingPolicyScroll))
        {
            const char* tabs[] = { "Scene", "Environment", "Shadows", "Post FX", "Lightmaps", "Probes", "Light Explorer" };
            for (int i = 0; i < 7; ++i)
            {
                ImGuiTabItemFlags flags = (m_RequestedTab == i) ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(tabs[i], nullptr, flags))
                {
                    ImGui::BeginChild("##tabcontent", ImVec2(0, 0), ImGuiChildFlags_None);
                    switch (i)
                    {
                        case 0: DrawSceneTab(); break;
                        case 1: DrawEnvironmentTab(); break;
                        case 2: DrawShadowsTab(); break;
                        case 3: DrawPostTab(); break;
                        case 4: DrawLightmapsTab(); break;
                        case 5: DrawProbesTab(); break;
                        case 6: DrawLightExplorerTab(); break;
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            m_RequestedTab = -1;
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void LightingPanel::DrawToolbar()
    {
        auto& bake = m_Context->GetRenderSettings().Bake;

        if (m_Bake.Active && !m_Bake.Silent)
        {
            std::string step;
            {
                std::lock_guard<std::mutex> lock(m_Bake.StepMutex);
                step = m_Bake.Step;
            }
            float progress = m_Bake.Progress.load();
            float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_Bake.Start).count();
            char overlay[160];
            snprintf(overlay, sizeof(overlay), "%s  %d%%  (%.1fs)", step.c_str(), (int)(progress * 100.0f), elapsed);
            float cancelW = 70.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - cancelW - ImGui::GetStyle().ItemSpacing.x);
            UI::ProgressBarLabeled(progress, overlay, UI::Colors::Accent);
            ImGui::SameLine();
            if (UI::DangerButton("Cancel", ImVec2(cancelW, 0)))
                CancelBake();
        }
        else
        {
            float w = ImGui::GetContentRegionAvail().x;
            float autoW = 130.0f;
            if (UI::AccentButton("Generate Lighting", ImVec2(w - autoW - 90.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f, 28)))
                GenerateLighting();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("Bakes lightmaps (static objects), Adaptive Probe Volumes and reflection probes.");
            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(90.0f, 28)))
                ClearBakedData();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Checkbox("Auto Generate", &bake.AutoGenerate);
        }

        // Durum satırı
        if (!m_StatusMessage.empty() && std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StatusTime).count() < 6.0f)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::TextDim);
            ImGui::TextUnformatted(m_StatusMessage.c_str());
            ImGui::PopStyleColor();
        }
        else if (m_Bake.Active && m_Bake.Silent)
        {
            UI::StatusDot(UI::Colors::Warning);
            ImGui::SameLine();
            ImGui::TextDisabled("Auto generating lighting... %d%%", (int)(m_Bake.Progress.load() * 100.0f));
        }
        else if (m_BakedInfo.Valid)
        {
            UI::StatusDot(UI::Colors::Success);
            ImGui::SameLine();
            ImGui::TextDisabled("Lightmaps baked (%s)", m_BakedInfo.When.c_str());
        }
        else
        {
            UI::StatusDot(UI::Colors::TextDim);
            ImGui::SameLine();
            ImGui::TextDisabled("No baked lighting data");
        }
        // Generate / Clear / Auto Generate ile sekmeleri ayıran çizgi
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ===================================================================== Scene sekmesi

    void LightingPanel::DrawSceneTab()
    {
        auto& rs = m_Context->GetRenderSettings();
        auto& b = rs.Bake;

        // ---- Lighting Settings varlığı (Unity: Lighting Settings Asset + New / Clone)
        {
            static std::string watched;
            static std::filesystem::file_time_type watchedTime{};
            std::error_code ec;
            if (!b.SettingsAsset.empty())
            {
                // Dosya başka yerde değiştiyse (başka sahne / dış düzenleme) sahneye yükle
                const auto t = std::filesystem::last_write_time(SceneSerializer::ResolveAssetPath(b.SettingsAsset), ec);
                if (!ec && (watched != b.SettingsAsset || t != watchedTime))
                {
                    const std::string asset = b.SettingsAsset;
                    SceneSerializer::LoadLightingSettings(asset, b);
                    b.SettingsAsset = asset;
                    watched = asset;
                    watchedTime = t;
                }
            }
            const LightBakeSettings before = b;
            UI::BeginProperty("Lighting Settings Asset", "Bake settings stored in a .cqlighting file that several scenes can share. "
                                                          "Changes made here are written to the asset.");
            {
                std::string shown = b.SettingsAsset.empty() ? std::string("None (scene's own settings)")
                                                            : std::filesystem::path(b.SettingsAsset).filename().string();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2.0f * (ImGui::GetFrameHeight() + 4.0f) - 54.0f);
                ImGui::BeginDisabled();
                char buf[512];
                snprintf(buf, sizeof(buf), "%s", shown.c_str());
                ImGui::InputText("##lightingasset", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
                ImGui::EndDisabled();
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        std::string dropped((const char*)p->Data);
                        if (std::filesystem::path(dropped).extension() == ".cqlighting")
                        {
                            SceneSerializer::LoadLightingSettings(dropped, b);
                            b.SettingsAsset = dropped;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine(0.0f, 4.0f);
                std::string picked;
                if (ObjectPicker::Button("##pickLighting", "Lighting Settings", { ".cqlighting" }, b.SettingsAsset, picked))
                {
                    if (!picked.empty())
                        SceneSerializer::LoadLightingSettings(picked, b);
                    b.SettingsAsset = picked;
                }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button(b.SettingsAsset.empty() ? "New" : "Clone") && Project::GetActive())
                {
                    const std::filesystem::path dir = Project::GetActiveProjectDirectory() / "Assets";
                    std::filesystem::path file = dir / "Lighting Settings.cqlighting";
                    for (int i = 1; std::filesystem::exists(file, ec); ++i)
                        file = dir / ("Lighting Settings " + std::to_string(i) + ".cqlighting");
                    if (SceneSerializer::SaveLightingSettings(file.string(), b))
                        b.SettingsAsset = file.string();
                }
            }
            UI::EndProperty();
            UI::BeginProperty("Default Lightmap Parameters", "Bake quality (anti-aliasing, backface tolerance, pushoff) for renderers "
                                                              "that do not pick their own Lightmap Parameters.");
            {
                std::string shown = b.DefaultLightmapParameters.empty() ? std::string("Default (built-in)")
                                                                        : std::filesystem::path(b.DefaultLightmapParameters).filename().string();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4.0f);
                ImGui::BeginDisabled();
                char buf[512];
                snprintf(buf, sizeof(buf), "%s", shown.c_str());
                ImGui::InputText("##lmparams", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
                ImGui::EndDisabled();
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        std::string dropped((const char*)p->Data);
                        if (std::filesystem::path(dropped).extension() == ".cqlmparams")
                            b.DefaultLightmapParameters = dropped;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine(0.0f, 4.0f);
                std::string picked;
                if (ObjectPicker::Button("##pickLmParams", "Lightmap Parameters", { ".cqlmparams" }, b.DefaultLightmapParameters, picked))
                    b.DefaultLightmapParameters = picked;
            }
            UI::EndProperty();
            ImGui::Spacing();
            // Bu karede yapılan değişiklikler (alt bölümler dahil) sonraki karede dosyaya yazılır
            m_LightingAssetBefore = std::make_shared<LightBakeSettings>(before);
        }

        if (UI::BeginSection("Realtime Lighting"))
        {
            UI::Checkbox("Realtime Global Illumination", b.RealtimeGI,
                "Static objects marked 'Realtime GI' receive a low resolution lightmap that is regenerated automatically when lights change.");
            if (b.RealtimeGI)
            {
                UI::DragInt("Realtime Resolution", b.RealtimeResolution, 0.2f, 1, 200, "Texels per unit");
                UI::DragInt("Indirect Samples", b.RealtimeIndirectSamples, 4.0f, 8, 4096);
                UI::SliderInt("Max Bounces", b.RealtimeMaxBounces, 0, 4);
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Mixed Lighting"))
        {
            UI::Checkbox("Baked Global Illumination", b.BakedGI, "Bake indirect lighting of Baked/Mixed lights into lightmaps.");
            const char* modes[] = { "Shadowmask", "Distance Shadowmask", "Subtractive", "Baked Indirect" };
            UI::Combo("Lighting Mode", b.LightingMode, modes, 4,
                "Shadowmask: realtime direct + baked shadows. Subtractive: everything baked except the sun's dynamic shadows. Baked Indirect: only indirect light is baked.");
            UI::EndSection();
        }

        if (UI::BeginSection("Lightmapping Settings"))
        {
            const char* lightmappers[] = { "None", "Progressive CPU", "Progressive GPU (preview)" };
            UI::Combo("Lightmapper", b.Lightmapper, lightmappers, 3);
            UI::Checkbox("Importance Sampling", b.ImportanceSampling);
            UI::DragInt("Direct Samples", b.DirectSamples, 1.0f, 1, 4096);
            UI::DragInt("Indirect Samples", b.IndirectSamples, 4.0f, 8, 16384);
            UI::DragInt("Environment Samples", b.EnvironmentSamples, 4.0f, 8, 8192);
            UI::SliderInt("Max Bounces", b.MaxBounces, 0, 8);
            const char* filtering[] = { "Auto", "None" };
            UI::Combo("Filtering", b.Filtering, filtering, 2);
            UI::Separator();
            UI::DragInt("Lightmap Resolution", b.Resolution, 0.25f, 1, 400, "Texels per unit");
            const char* packing[] = { "Auto", "Manual" };
            UI::Combo("Lightmap Packing", b.Packing, packing, 2);
            const int sizes[] = { 256, 512, 1024, 2048, 4096 };
            const char* sizeNames[] = { "256", "512", "1024", "2048", "4096" };
            int sizeIndex = 2;
            for (int i = 0; i < 5; ++i)
                if (sizes[i] == b.MaxLightmapSize) sizeIndex = i;
            if (UI::Combo("Max Lightmap Size", sizeIndex, sizeNames, 5))
                b.MaxLightmapSize = sizes[sizeIndex];
            UI::Checkbox("Use Mipmap Limits", b.UseMipmapLimits);
            const char* compression[] = { "None", "Low Quality", "Normal Quality", "High Quality" };
            UI::Combo("Lightmap Compression", b.Compression, compression, 4);
            UI::Checkbox("Ambient Occlusion", b.AmbientOcclusion);
            const char* dirModes[] = { "Directional", "Non-Directional" };
            UI::Combo("Directional Mode", b.DirectionalMode, dirModes, 2);
            UI::SliderFloat("Albedo Boost", b.AlbedoBoost, 1.0f, 10.0f);
            UI::SliderFloat("Indirect Intensity", b.IndirectIntensity, 0.0f, 5.0f);
            UI::EndSection();
        }

        // Lighting Settings varlığına yaz (bu karede ayar değiştiyse)
        if (!b.SettingsAsset.empty() && m_LightingAssetBefore)
        {
            auto dump = [](const LightBakeSettings& s) {
                LightBakeSettings c = s;
                c.SettingsAsset.clear();
                return std::string(SceneSerializer::LightingSettingsToString(c));
            };
            if (dump(*m_LightingAssetBefore) != dump(b) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                SceneSerializer::SaveLightingSettings(b.SettingsAsset, b);
            else if (dump(*m_LightingAssetBefore) != dump(b))
                m_PendingLightingSave = true;
        }
        if (m_PendingLightingSave && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !b.SettingsAsset.empty())
        {
            SceneSerializer::SaveLightingSettings(b.SettingsAsset, b);
            m_PendingLightingSave = false;
        }

        if (UI::BeginSection("Workflow", false))
        {
            const char* onLoad[] = { "Never", "If lighting data is missing" };
            UI::Combo("Bake On Scene Load", b.BakeOnSceneLoad, onLoad, 2);
            UI::Checkbox("Auto Generate", b.AutoGenerate, "Regenerates lighting one second after a light or static object changes.");

            // Statik nesne sayısı (bake'e katılanlar)
            int staticCount = 0, totalCount = 0;
            auto view = m_Context->m_Registry.view<TagComponent, MeshRendererComponent>();
            for (auto e : view)
            {
                ++totalCount;
                if (view.get<TagComponent>(e).IsStatic)
                    ++staticCount;
            }
            UI::Text("Static Renderers", "%d / %d", staticCount, totalCount);
            if (staticCount == 0 && totalCount > 0)
                UI::InfoBox("No renderer is marked Static. Only static objects contribute to baked lightmaps (Inspector > Static).", UI::InfoType::Warning);
            UI::EndSection();
        }
    }

    // ===================================================================== Environment sekmesi

    void LightingPanel::DrawSkyboxPicker()
    {
        auto now = std::chrono::steady_clock::now();
        if (m_SkyboxCandidates.empty() || std::chrono::duration<float>(now - m_LastSkyboxScan).count() > 4.0f)
        {
            m_LastSkyboxScan = now;
            m_SkyboxCandidates.clear();
            std::error_code ec;
            auto scan = [&](const std::filesystem::path& root, bool recursive)
            {
                if (!std::filesystem::is_directory(root, ec))
                    return;
                auto consider = [&](const std::filesystem::directory_entry& entry)
                {
                    if (!entry.is_regular_file(ec))
                        return;
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".hdr" || ext == ".exr" || ext == ".cqcubemap")
                        m_SkyboxCandidates.push_back(entry.path().string());
                };
                if (recursive)
                {
                    for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
                         it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                        consider(*it);
                }
                else
                {
                    for (auto& entry : std::filesystem::directory_iterator(root, ec))
                        consider(entry);
                }
            };
            scan(EngineDirectory() / "Resources" / "skybox", false);
            auto projectDir = Project::GetActiveProjectDirectory();
            if (!projectDir.empty())
                scan(projectDir / "Assets", true);
            std::sort(m_SkyboxCandidates.begin(), m_SkyboxCandidates.end());
            m_SkyboxCandidates.erase(std::unique(m_SkyboxCandidates.begin(), m_SkyboxCandidates.end()), m_SkyboxCandidates.end());
        }

        auto skybox = m_Context->GetSkybox();
        std::string current = skybox ? std::filesystem::path(skybox->GetPath()).stem().string() : "None (Procedural / Color)";

        auto loadSkybox = [&](const std::string& path)
        {
            int resolution = 1024;
            if (m_SkyboxResolutionIndex == 4)
            {
                int w = 0, h = 0, c = 0;
                if (stbi_info(path.c_str(), &w, &h, &c))
                    resolution = std::clamp(w / 4, 256, 4096);
            }
            else
            {
                const int res[] = { 512, 1024, 2048, 4096 };
                resolution = res[std::clamp(m_SkyboxResolutionIndex, 0, 3)];
            }
            auto cube = Cubemap::CreateFromEquirectangular(path, (uint32_t)resolution);
            if (cube && cube->GetRendererID())
            {
                m_Context->SetSkybox(cube);
                m_Context->InvalidateReflectionProbes();
                m_StatusMessage = "Skybox loaded: " + std::filesystem::path(path).filename().string();
            }
            else
            {
                m_StatusMessage = "Failed to load skybox: " + path;
            }
            m_StatusTime = std::chrono::steady_clock::now();
        };

        UI::BeginProperty("Skybox");
        const ImGuiID skyPick = ImGui::GetID("##skypick");
        if (std::string picked; ObjectPicker::Consume(skyPick, picked) && !picked.empty())
            loadSkybox(picked);
        if (ImGui::BeginCombo("##skybox", current.c_str()))
        {
            if (ImGui::Selectable("None (Procedural / Color)", !skybox))
            {
                m_Context->SetSkybox(nullptr);
                m_Context->InvalidateReflectionProbes();
            }
            ImGui::Separator();
            for (const auto& path : m_SkyboxCandidates)
            {
                std::string name = std::filesystem::path(path).stem().string();
                bool selected = skybox && skybox->GetPath() == path;
                if (ImGui::Selectable((name + "##" + path).c_str(), selected))
                    loadSkybox(path);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("%s", path.c_str());
            }
            ImGui::Separator();
            if (ImGui::Selectable(ICON_FA_BULLSEYE "  Select..."))
                ObjectPicker::Open(skyPick, "Sky (HDR / Cubemap)", { ".hdr", ".exr", ".cqcubemap" }, skybox ? skybox->GetPath() : std::string());
            if (ImGui::Selectable("Browse..."))
            {
                nfdu8char_t* outPath = nullptr;
                nfdfilteritem_t filters[1] = { { "Sky (HDR / Cubemap)", "hdr,exr,cqcubemap" } };
                if (NFD_OpenDialogU8(&outPath, filters, 1, nullptr) == NFD_OKAY)
                {
                    loadSkybox(outPath);
                    NFD_FreePathU8(outPath);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::string path((const char*)payload->Data);
                std::string ext = std::filesystem::path(path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".hdr" || ext == ".exr")
                    loadSkybox(path);
            }
            ImGui::EndDragDropTarget();
        }
        UI::EndProperty();

        const char* resNames[] = { "512", "1024", "2048", "4096", "Source / 4" };
        UI::Combo("Cubemap Resolution", m_SkyboxResolutionIndex, resNames, 5, "Resolution used when converting the next HDR file to a cubemap.");
    }

    void LightingPanel::DrawTexturePicker(const char* label, std::shared_ptr<Texture2D> texture, const std::function<void(std::shared_ptr<Texture2D>)>& setter)
    {
        UI::BeginProperty(label);
        float h = ImGui::GetFrameHeight() * 2.0f;
        if (texture && texture->GetRendererID())
            ImGui::Image((ImTextureID)(intptr_t)texture->GetRendererID(), ImVec2(h, h), ImVec2(0, 1), ImVec2(1, 0));
        else
            ImGui::Button("None", ImVec2(h, h));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                auto tex = Texture2D::Create(std::string((const char*)payload->Data));
                if (tex && tex->GetRendererID())
                    setter(tex);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        std::string name = texture ? std::filesystem::path(texture->GetPath()).filename().string() : std::string("None (Texture 2D)");
        ImGui::TextUnformatted(name.c_str());
        if (ImGui::SmallButton("Browse"))
        {
            nfdu8char_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = { { "Images", "png,jpg,jpeg,tga,bmp" } };
            if (NFD_OpenDialogU8(&outPath, filters, 1, nullptr) == NFD_OKAY)
            {
                auto tex = Texture2D::Create(std::string(outPath));
                if (tex && tex->GetRendererID())
                    setter(tex);
                NFD_FreePathU8(outPath);
            }
        }
        ImGui::SameLine();
        if (std::string picked; ObjectPicker::Button("lighttexpick", "Texture", ObjectPicker::ImageExtensions(),
                                                     texture ? texture->GetPath() : std::string(), picked))
        {
            auto tex = picked.empty() ? nullptr : Texture2D::Create(picked);
            setter(tex && tex->GetRendererID() ? tex : nullptr);
        }
        if (texture)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
                setter(nullptr);
        }
        ImGui::EndGroup();
        UI::EndProperty();
    }

    void LightingPanel::DrawEnvironmentTab()
    {
        auto& rs = m_Context->GetRenderSettings();
        auto& env = rs.Environment;

        if (UI::BeginSection("Sky"))
        {
            const char* skyModes[] = { "Auto", "Procedural", "Solid Color", "Sky Atmosphere" };
            UI::ComboEnum("Sky Mode", env.Sky, skyModes, 4,
                "Auto: the skybox if assigned, otherwise a procedural sky for 3D scenes and a solid color for 2D scenes. "
                "Sky Atmosphere: physically based sky (Rayleigh + Mie + ozone) that follows the sun, with sunsets and twilight.");
            if (env.Sky == SkyMode::Atmosphere)
            {
                UI::SliderFloat("Rayleigh", env.AtmosphereRayleigh, 0.0f, 4.0f, "%.2f", "Blue sky scattering (higher = deeper blue, redder sunsets)");
                UI::SliderFloat("Mie", env.AtmosphereMie, 0.0f, 10.0f, "%.2f", "Haze / aerosols: whiter sky and a larger sun glow");
                UI::SliderFloat("Mie Anisotropy", env.AtmosphereMieAnisotropy, 0.0f, 0.98f, "%.2f", "How strongly haze scatters forward around the sun");
                UI::SliderFloat("Ozone", env.AtmosphereOzone, 0.0f, 4.0f, "%.2f", "Ozone absorption (purple twilight colors)");
                UI::SliderFloat("Sun Disk Size", env.AtmosphereSunDisk, 0.0f, 4.0f);
                UI::SliderFloat("Intensity", env.AtmosphereIntensity, 0.0f, 8.0f);
                UI::Color3("Ground Albedo", env.AtmosphereGroundAlbedo, "Color of the planet surface below the horizon");
            }
            else if (env.Sky != SkyMode::SolidColor)
            {
                DrawSkyboxPicker();
                if (m_Context->GetSkybox())
                {
                    float exposure = m_Context->GetSkyboxExposure();
                    if (UI::SliderFloat("Exposure", exposure, 0.0f, 8.0f))
                        m_Context->SetSkyboxExposure(exposure);
                    float rotation = m_Context->GetSkyboxRotation();
                    if (UI::SliderFloat("Rotation", rotation, 0.0f, 360.0f, "%.0f°"))
                        m_Context->SetSkyboxRotation(rotation);
                    glm::vec3 tint = m_Context->GetSkyboxTint();
                    if (UI::Color3("Tint", tint))
                        m_Context->SetSkyboxTint(tint);
                }
                else
                {
                    UI::InfoBox("No skybox assigned: a procedural sky that follows the Sun Source is used.", UI::InfoType::Info);
                }
            }
            UI::Color3("Background Color", env.BackgroundColor, "Used when the sky is not drawn (Solid Color mode or 2D scenes).");

            // Güneş
            Entity sunEntity = m_Context->GetSunSourceEntity();
            std::string sunName = sunEntity && sunEntity.HasComponent<TagComponent>() ? sunEntity.GetComponent<TagComponent>().Tag : "None (Light)";
            UI::BeginProperty("Sun Source", "Directional light used for the sun disk, procedural sky and cascaded shadows.");
            if (ImGui::BeginCombo("##sun", sunName.c_str()))
            {
                if (ImGui::Selectable("None (first directional light)", m_Context->GetSunSource() == 0))
                    m_Context->SetSunSource(0);
                auto view = m_Context->m_Registry.view<TagComponent, IDComponent, DirectionalLightComponent>();
                for (auto e : view)
                {
                    auto& tag = view.get<TagComponent>(e);
                    auto& id = view.get<IDComponent>(e);
                    std::string label = tag.Tag + "##sun" + std::to_string((uint64_t)id.ID);
                    if (ImGui::Selectable(label.c_str(), m_Context->GetSunSource() == (uint64_t)id.ID))
                        m_Context->SetSunSource(id.ID);
                }
                ImGui::EndCombo();
            }
            UI::EndProperty();
            UI::EndSection();
        }

        if (UI::BeginSection("Environment Lighting"))
        {
            const char* sources[] = { "Skybox", "Color", "Gradient" };
            int source = (int)m_Context->GetEnvironmentLightingSource();
            if (UI::Combo("Source", source, sources, 3))
                m_Context->SetEnvironmentLightingSource((Scene::EnvironmentLightingSource)source);

            if (source == 2)
            {
                glm::vec3 sky = m_Context->GetAmbientSkyColor();
                if (UI::Color3("Sky Color", sky)) m_Context->SetAmbientSkyColor(sky);
                glm::vec3 equator = m_Context->GetAmbientEquatorColor();
                if (UI::Color3("Equator Color", equator)) m_Context->SetAmbientEquatorColor(equator);
                glm::vec3 ground = m_Context->GetAmbientGroundColor();
                if (UI::Color3("Ground Color", ground)) m_Context->SetAmbientGroundColor(ground);
            }
            else
            {
                glm::vec3 ambient = m_Context->GetAmbientColor();
                const char* label = source == 0 ? "Fallback Color" : "Ambient Color";
                if (UI::Color3(label, ambient, source == 0 ? "Used when Image Based Lighting is disabled." : nullptr))
                    m_Context->SetAmbientColor(ambient);
            }

            float intensity = m_Context->GetAmbientIntensity();
            if (UI::SliderFloat("Intensity Multiplier", intensity, 0.0f, 8.0f))
                m_Context->SetAmbientIntensity(intensity);
            if (source == 0)
                UI::Checkbox("Image Based Lighting", env.ImageBasedLighting, "Diffuse (spherical harmonics) and specular (prefiltered cubemap) lighting from the sky.");
            UI::EndSection();
        }

        if (UI::BeginSection("Environment Reflections"))
        {
            UI::SliderFloat("Intensity Multiplier##refl", env.ReflectionIntensity, 0.0f, 4.0f);
            UI::SliderFloat("Sky Contribution", env.SkyboxIntensity, 0.0f, 4.0f, "%.2f", "Scales ambient light and reflections that come from the sky.");
            UI::Checkbox("Specular Occlusion", env.SpecularOcclusion);
            int resolution = m_Context->GetReflectionResolution();
            const char* resNames[] = { "64", "128", "256", "512", "1024" };
            const int resValues[] = { 64, 128, 256, 512, 1024 };
            int resIndex = 1;
            for (int i = 0; i < 5; ++i)
                if (resValues[i] == resolution) resIndex = i;
            if (UI::Combo("Probe Resolution", resIndex, resNames, 5, "Default resolution for newly created reflection probes."))
                m_Context->SetReflectionResolution(resValues[resIndex]);
            UI::EndSection();
        }

        if (UI::BeginSection("Fog"))
        {
            auto& fog = rs.Fog;
            UI::Checkbox("Enabled##fog", fog.Enabled);
            if (fog.Enabled)
            {
                const char* modes[] = { "Linear", "Exponential", "Exponential Squared" };
                UI::ComboEnum("Mode", fog.Mode, modes, 3);
                UI::Color3("Color", fog.Color);
                if (fog.Mode == FogMode::Linear)
                {
                    UI::DragFloat("Start", fog.Start, 0.5f, 0.0f, 100000.0f, "%.1f");
                    UI::DragFloat("End", fog.End, 0.5f, 0.0f, 100000.0f, "%.1f");
                    if (fog.End <= fog.Start)
                        fog.End = fog.Start + 0.1f;
                }
                else
                {
                    UI::DragFloat("Density", fog.Density, 0.0005f, 0.0f, 2.0f, "%.4f");
                }
                UI::DragFloat("Height Falloff", fog.HeightFalloff, 0.005f, 0.0f, 10.0f, "%.3f", "0 = uniform fog. Larger values thin the fog with altitude.");
                if (fog.HeightFalloff > 0.0f)
                    UI::DragFloat("Base Height", fog.BaseHeight, 0.1f, -10000.0f, 10000.0f, "%.1f");
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Halo, Flare & Cookie", false))
        {
            bool halo = m_Context->IsHaloEnabled();
            if (UI::Checkbox("Halo", halo)) m_Context->SetHaloEnabled(halo);
            if (halo)
            {
                DrawTexturePicker("Halo Texture", m_Context->GetHaloTexture(), [this](std::shared_ptr<Texture2D> t) { m_Context->SetHaloTexture(t); });
                float strength = m_Context->GetHaloStrength();
                if (UI::SliderFloat("Halo Strength", strength, 0.0f, 1.0f)) m_Context->SetHaloStrength(strength);
            }

            bool flare = m_Context->IsFlareEnabled();
            if (UI::Checkbox("Lens Flare", flare)) m_Context->SetFlareEnabled(flare);
            if (flare)
            {
                float fade = m_Context->GetFlareFadeSpeed();
                if (UI::DragFloat("Fade Speed", fade, 0.05f, 0.0f, 20.0f)) m_Context->SetFlareFadeSpeed(fade);
                float fs = m_Context->GetFlareStrength();
                if (UI::SliderFloat("Flare Strength", fs, 0.0f, 5.0f)) m_Context->SetFlareStrength(fs);

                int count = m_Context->GetFlareElementCount();
                UI::SubHeader("Flare Elements");
                for (int i = 0; i < count; ++i)
                {
                    ImGui::PushID(i);
                    auto element = m_Context->GetFlareElement(i);
                    char title[32];
                    snprintf(title, sizeof(title), "Element %d", i + 1);
                    bool open = ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_SpanAvailWidth);
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.0f);
                    bool remove = ImGui::SmallButton("x");
                    if (open)
                    {
                        bool changed = false;
                        changed |= UI::Color3("Color", element.ColorMultiplier);
                        changed |= UI::SliderFloat("Size", element.Size, 0.01f, 0.5f);
                        changed |= UI::SliderFloat("Offset", element.Offset, -1.0f, 2.0f);
                        changed |= UI::SliderFloat("Intensity", element.Intensity, 0.0f, 2.0f);
                        if (changed)
                            m_Context->SetFlareElement(i, element);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                    if (remove)
                    {
                        // Elemanı sil: sonrakileri kaydır
                        for (int j = i; j < count - 1; ++j)
                            m_Context->SetFlareElement(j, m_Context->GetFlareElement(j + 1));
                        m_Context->SetFlareElementCount(count - 1);
                        break;
                    }
                }
                if (ImGui::Button("+ Add Element"))
                    m_Context->SetFlareElementCount(count + 1);
                ImGui::SameLine();
                if (count == 0 && ImGui::Button("Add Default Set"))
                    m_Context->SetFlareElementCount(3);
            }

            DrawTexturePicker("Spot Cookie", m_Context->GetSpotCookie(), [this](std::shared_ptr<Texture2D> t) { m_Context->SetSpotCookie(t); });
            UI::EndSection();
        }
    }

    // ===================================================================== Shadows sekmesi

    void LightingPanel::DrawCascadePreview()
    {
        const auto& sh = m_Context->GetRenderSettings().Shadows;
        int count = std::clamp(sh.CascadeCount, 1, 4);
        float nearPlane = 0.1f, farPlane = std::max(sh.Distance, 1.0f);
        float splits[4];
        for (int i = 0; i < count; ++i)
        {
            float p = (float)(i + 1) / (float)count;
            float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
            float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
            splits[i] = sh.CascadeSplitLambda * logSplit + (1.0f - sh.CascadeSplitLambda) * uniformSplit;
        }

        const ImU32 colors[4] = { IM_COL32(86, 156, 214, 255), IM_COL32(106, 190, 120, 255), IM_COL32(220, 180, 90, 255), IM_COL32(210, 100, 100, 255) };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = 22.0f;
        float prev = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            float x0 = p.x + w * (prev / farPlane);
            float x1 = p.x + w * (splits[i] / farPlane);
            dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + h), colors[i], i == 0 ? 3.0f : 0.0f);
            char label[48];
            snprintf(label, sizeof(label), "%d  %.1fm", i, splits[i]);
            ImVec2 ts = ImGui::CalcTextSize(label);
            if (ts.x < (x1 - x0) - 4.0f)
                dl->AddText(ImVec2((x0 + x1) * 0.5f - ts.x * 0.5f, p.y + (h - ts.y) * 0.5f), IM_COL32(15, 15, 18, 255), label);
            prev = splits[i];
        }
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(0, 0, 0, 120), 3.0f);
        ImGui::Dummy(ImVec2(w, h + 4.0f));
    }

    void LightingPanel::DrawShadowsTab()
    {
        auto& sh = m_Context->GetRenderSettings().Shadows;

        if (UI::BeginSection("Realtime Shadows"))
        {
            UI::Checkbox("Enabled", sh.Enabled);
            const char* quality[] = { "Hard", "Soft (Poisson PCF)", "Very Soft (PCSS)" };
            UI::ComboEnum("Quality", sh.Quality, quality, 3, "Very Soft computes a contact-hardening penumbra from the blocker distance.");
            const int resolutions[] = { 1024, 2048, 4096, 8192 };
            const char* resNames[] = { "1024", "2048", "4096", "8192" };
            int resIndex = 2;
            for (int i = 0; i < 4; ++i)
                if (resolutions[i] == sh.Resolution) resIndex = i;
            if (UI::Combo("Resolution", resIndex, resNames, 4, "Size of the shadow atlas. Each cascade gets a quarter of it (two cascades: half)."))
                sh.Resolution = resolutions[resIndex];
            UI::DragFloat("Shadow Distance", sh.Distance, 0.5f, 1.0f, 5000.0f, "%.1f m");
            UI::SliderFloat("Strength", sh.Strength, 0.0f, 1.0f);
            UI::SliderFloat("Softness", sh.Softness, 0.0f, 4.0f);
            UI::Color3("Shadow Color", sh.Color, "Tint of fully shadowed areas (black = physically correct).");
            UI::EndSection();
        }

        if (UI::BeginSection("Cascades"))
        {
            UI::SliderInt("Cascade Count", sh.CascadeCount, 1, 4);
            UI::SliderFloat("Split Distribution", sh.CascadeSplitLambda, 0.0f, 1.0f, "%.2f", "0 = uniform splits, 1 = logarithmic (more resolution near the camera).");
            DrawCascadePreview();
            UI::EndSection();
        }

        if (UI::BeginSection("Bias"))
        {
            UI::DragFloat("Depth Bias", sh.DepthBias, 0.0005f, 0.0f, 0.1f, "%.4f", "Removes shadow acne; too high causes peter-panning.");
            UI::DragFloat("Normal Bias", sh.NormalBias, 0.001f, 0.0f, 0.5f, "%.3f");
            if (ImGui::Button("Reset Bias"))
            {
                sh.DepthBias = ShadowSettings{}.DepthBias;
                sh.NormalBias = ShadowSettings{}.NormalBias;
            }
            UI::EndSection();
        }

        // Güneş gölge durumu bilgisi
        Entity sun = m_Context->GetSunSourceEntity();
        if (!sun || !sun.HasComponent<DirectionalLightComponent>())
            UI::InfoBox("The scene has no directional light: add one (GameObject > Light > Directional Light) to get sun shadows.", UI::InfoType::Warning);
        else if (!sun.GetComponent<DirectionalLightComponent>().CastShadows)
            UI::InfoBox("The sun light has 'Cast Shadows' disabled in the Inspector.", UI::InfoType::Warning);
        else
            UI::Text("Active Cascades", "%d", Renderer3D::GetShadowPass().GetActiveCascadeCount());
    }

    // ===================================================================== Post FX sekmesi

    void LightingPanel::ApplyPostPreset(int preset)
    {
        auto& p = m_Context->GetRenderSettings().Post;
        PostProcessSettings defaults;
        // Önayarlar görünümü değiştirir; kenar yumuşatma ve yükseltme ayarları korunur
        const PostProcessSettings keep = p;
        p = defaults;
        p.AntiAliasing = keep.AntiAliasing;
        p.MSAASamples = keep.MSAASamples;
        p.EQAASamples = keep.EQAASamples;
        p.SSAAScale = keep.SSAAScale;
        p.TAAFeedback = keep.TAAFeedback;
        p.TAASharpness = keep.TAASharpness;
        p.TemporalStability = keep.TemporalStability;
        p.Upscaler = keep.Upscaler;
        p.UpscaleQuality = keep.UpscaleQuality;
        p.UpscaleRenderScale = keep.UpscaleRenderScale;
        p.UpscaleSharpness = keep.UpscaleSharpness;
        switch (preset)
        {
            case 1: // Cinematic
                p.Tonemap = Tonemapper::ACES; p.Bloom = true; p.BloomIntensity = 0.8f; p.Vignette = true; p.VignetteIntensity = 0.35f;
                p.FilmGrain = true; p.FilmGrainIntensity = 0.12f; p.Contrast = 1.1f; p.Saturation = 0.95f; p.Temperature = 0.1f;
                break;
            case 2: // Vibrant
                p.Tonemap = Tonemapper::AgX; p.Saturation = 1.25f; p.Contrast = 1.08f; p.Bloom = true; p.BloomIntensity = 0.7f; p.Sharpen = true;
                break;
            case 3: // Retro
                p.Tonemap = Tonemapper::Reinhard; p.ChromaticAberration = true; p.ChromaticAberrationIntensity = 0.6f; p.FilmGrain = true;
                p.FilmGrainIntensity = 0.3f; p.Vignette = true; p.VignetteIntensity = 0.5f; p.Saturation = 0.8f; p.Temperature = 0.35f;
                break;
            case 4: // Noir
                p.Tonemap = Tonemapper::ACES; p.Saturation = 0.0f; p.Contrast = 1.3f; p.Vignette = true; p.VignetteIntensity = 0.55f; p.FilmGrain = true;
                break;
            case 5: // Off
                p.Enabled = false;
                break;
            default:
                break;
        }
    }

    void LightingPanel::DrawPostTab()
    {
        auto& p = m_Context->GetRenderSettings().Post;

        // Hazır ayarlar
        const char* presets[] = { "Default", "Cinematic", "Vibrant", "Retro", "Noir", "Off" };
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 5.0f) / 6.0f;
        for (int i = 0; i < 6; ++i)
        {
            if (i > 0) ImGui::SameLine();
            if (ImGui::Button(presets[i], ImVec2(bw, 0)))
                ApplyPostPreset(i);
        }
        ImGui::Spacing();

        UI::Checkbox("Post Processing", p.Enabled);
        ImGui::BeginDisabled(!p.Enabled);

        if (UI::BeginSection("Tonemapping & Exposure"))
        {
            const char* tonemappers[] = { "None", "Neutral (Khronos PBR)", "ACES", "AgX", "Reinhard" };
            UI::ComboEnum("Tonemapper", p.Tonemap, tonemappers, 5, "Neutral keeps colors below 0.76 untouched (ideal for 2D/UI); ACES and AgX give a filmic look.");
            UI::SliderFloat("Exposure (EV)", p.Exposure, -6.0f, 6.0f, "%+.2f");
            UI::Checkbox("Auto Exposure", p.AutoExposure, "Eye adaptation: exposure follows the average scene brightness over time (Unreal Auto Exposure).");
            if (p.AutoExposure)
            {
                UI::SliderFloat("Min EV", p.AutoExposureMin, -10.0f, 10.0f, "%+.1f");
                UI::SliderFloat("Max EV", p.AutoExposureMax, -10.0f, 10.0f, "%+.1f");
                UI::SliderFloat("Speed Up", p.AutoExposureSpeedUp, 0.05f, 20.0f, "%.2f", "Adaptation speed when the scene gets brighter");
                UI::SliderFloat("Speed Down", p.AutoExposureSpeedDown, 0.05f, 20.0f, "%.2f", "Adaptation speed when the scene gets darker");
                UI::SliderFloat("Compensation", p.AutoExposureCompensation, -6.0f, 6.0f, "%+.2f");
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Global Illumination (Ray Traced)"))
        {
            UI::Checkbox("Enabled##rtgi", p.RayTracedGI, "Lumen-style dynamic global illumination: light probes around the camera trace rays against the scene "
                                                         "(sky, sun and lights with shadows, emissive surfaces, multiple bounces) and replace the ambient light. "
                                                         "Rooms get dark where the sky cannot reach, lit surfaces brighten and tint their surroundings. No baking: "
                                                         "updates by itself when lights or objects move. Surfaces with baked lightmaps keep their baked lighting.");
            if (p.RayTracedGI)
            {
                UI::SliderFloat("Intensity##rtgi", p.RTGIIntensity, 0.0f, 4.0f, "%.2f", "Indirect lighting intensity");
                UI::SliderFloat("Probe Spacing##rtgi", p.RTGIProbeSpacing, 0.5f, 8.0f, "%.2f m", "Distance between light probes: smaller gives finer detail and costs more");
                UI::SliderFloat("View Distance##rtgi", p.RTGIViewDistance, 4.0f, 96.0f, "%.0f m", "How far around the camera the probes reach; beyond it the ambient light is used");
                UI::SliderInt("Rays per Probe##rtgi", p.RTGIRaysPerProbe, 32, 256, "Final gather quality: more rays give smoother light, probes update less often");
                UI::SliderFloat("Update Speed##rtgi", p.RTGIUpdateSpeed, 0.1f, 4.0f, "%.2f", "How fast the lighting follows changes (higher reacts faster, may flicker)");
                UI::SliderFloat("Skylight Leaking##rtgi", p.RTGISkyLeaking, 0.0f, 1.0f, "%.2f", "Part of the sky light that still reaches closed places");
                UI::SliderFloat("Diffuse Color Boost##rtgi", p.RTGIDiffuseBoost, 0.1f, 4.0f, "%.2f", "Strength of surface colors in bounced light");
                const auto& st = RayTracedGI::GetStats();
                if (st.Active)
                {
                    ImGui::TextDisabled("%d probes (%dx%dx%d), %d updated this frame", st.Probes, st.Counts.x, st.Counts.y, st.Counts.z, st.ProbesUpdated);
                    ImGui::TextDisabled("Scene: %d triangles, %d instances%s", st.Triangles, st.Instances, st.PendingBuilds > 0 ? " (preparing meshes...)" : "");
                }
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Global Illumination (Screen Traces)"))
        {
            UI::Checkbox("Enabled##ssgi", p.ScreenSpaceGI, "Lumen-style dynamic diffuse bounce light traced on screen: lit surfaces tint and brighten their neighbours. "
                                                           "No baking; works with moving lights and objects (PBR materials). Not used while Ray Traced GI is on.");
            if (p.ScreenSpaceGI && p.RayTracedGI)
                ImGui::TextDisabled("Ray Traced GI is on: screen traces are not used.");
            if (p.ScreenSpaceGI)
            {
                UI::SliderFloat("Intensity##ssgi", p.SSGIIntensity, 0.0f, 4.0f);
                UI::SliderFloat("Ray Length##ssgi", p.SSGIRadius, 0.25f, 20.0f, "%.2f m");
                UI::SliderInt("Rays per Pixel##ssgi", p.SSGIRays, 1, 16);
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Screen Space Reflections"))
        {
            UI::Checkbox("Enabled##ssr", p.ScreenSpaceReflections, "Glossy floors, water and metals reflect what is on screen (PBR materials).");
            if (p.ScreenSpaceReflections)
            {
                UI::SliderFloat("Intensity##ssr", p.SSRIntensity, 0.0f, 2.0f);
                UI::SliderFloat("Max Distance##ssr", p.SSRMaxDistance, 1.0f, 200.0f, "%.1f m");
                UI::SliderFloat("Thickness##ssr", p.SSRThickness, 0.01f, 4.0f, "%.2f m", "How thick objects are assumed to be (reduces holes, may add streaks)");
                UI::SliderFloat("Max Roughness##ssr", p.SSRMaxRoughness, 0.05f, 1.0f, "%.2f", "Rougher surfaces do not get screen space reflections");
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Volumetric Fog"))
        {
            UI::Checkbox("Enabled##vfog", p.VolumetricFog, "Height fog lit by the sun with shadowed light shafts (god rays) and sky light.");
            if (p.VolumetricFog)
            {
                UI::DragFloat("Density##vfog", p.VolumetricFogDensity, 0.0005f, 0.0f, 1.0f, "%.4f");
                UI::DragFloat("Height Falloff##vfog", p.VolumetricFogHeightFalloff, 0.002f, 0.0f, 5.0f, "%.3f", "Fog thins out with height (0 = uniform)");
                UI::DragFloat("Base Height##vfog", p.VolumetricFogBaseHeight, 0.05f, -10000.0f, 10000.0f, "%.2f m");
                UI::SliderFloat("Scattering Distribution##vfog", p.VolumetricFogAnisotropy, -0.9f, 0.95f, "%.2f", "> 0: brighter when looking towards the sun");
                UI::SliderFloat("Sun Intensity##vfog", p.VolumetricFogIntensity, 0.0f, 10.0f);
                UI::SliderFloat("Sky Light##vfog", p.VolumetricFogAmbient, 0.0f, 4.0f);
                UI::Color3("Albedo##vfog", p.VolumetricFogAlbedo);
                UI::DragFloat("View Distance##vfog", p.VolumetricFogDistance, 0.5f, 1.0f, 2000.0f, "%.0f m");
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Volumetric Clouds"))
        {
            UI::Checkbox("Enabled##clouds", p.VolumetricClouds, "Raymarched clouds drawn over the sky; they drift with the wind.");
            if (p.VolumetricClouds)
            {
                UI::SliderFloat("Coverage##clouds", p.CloudCoverage, 0.0f, 1.0f);
                UI::SliderFloat("Density##clouds", p.CloudDensity, 0.0f, 4.0f);
                UI::DragFloat("Layer Bottom##clouds", p.CloudBottom, 10.0f, 50.0f, 20000.0f, "%.0f m");
                UI::DragFloat("Layer Thickness##clouds", p.CloudThickness, 10.0f, 100.0f, 10000.0f, "%.0f m");
                UI::SliderFloat("Shape Scale##clouds", p.CloudScale, 0.1f, 5.0f, "%.2f", "Larger = smaller, more frequent clouds");
                UI::SliderFloat("Wind Speed##clouds", p.CloudWindSpeed, 0.0f, 100.0f, "%.1f m/s");
                UI::SliderFloat("Wind Direction##clouds", p.CloudWindDirection, 0.0f, 360.0f, "%.0f°");
                UI::Color3("Color##clouds", p.CloudColor);
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Depth of Field", false))
        {
            UI::Checkbox("Enabled##dof", p.DepthOfField, "Cinematic bokeh blur outside the focus distance.");
            if (p.DepthOfField)
            {
                UI::DragFloat("Focus Distance##dof", p.FocusDistance, 0.05f, 0.05f, 10000.0f, "%.2f m");
                UI::SliderFloat("Aperture (f-stop)##dof", p.Aperture, 0.7f, 32.0f, "f/%.1f", "Smaller = stronger blur");
                UI::SliderFloat("Focal Length##dof", p.FocalLength, 10.0f, 300.0f, "%.0f mm");
                UI::SliderFloat("Max Blur##dof", p.DOFMaxBlur, 1.0f, 32.0f, "%.0f px");
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Motion Blur", false))
        {
            UI::Checkbox("Enabled##mb", p.MotionBlur, "Blur from camera movement (reprojected from depth).");
            if (p.MotionBlur)
            {
                UI::SliderFloat("Intensity##mb", p.MotionBlurIntensity, 0.0f, 1.5f, "%.2f", "Shutter: fraction of the frame time");
                UI::SliderFloat("Max Length##mb", p.MotionBlurMax, 1.0f, 96.0f, "%.0f px");
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Bloom"))
        {
            UI::Checkbox("Enabled##bloom", p.Bloom);
            if (p.Bloom)
            {
                UI::DragFloat("Threshold", p.BloomThreshold, 0.01f, 0.0f, 20.0f);
                UI::SliderFloat("Knee", p.BloomKnee, 0.0f, 1.0f);
                UI::SliderFloat("Intensity", p.BloomIntensity, 0.0f, 4.0f);
                UI::SliderFloat("Scatter", p.BloomScatter, 0.0f, 1.0f);
                UI::Color3("Tint", p.BloomTint);
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Ambient Occlusion (SSAO)"))
        {
            UI::Checkbox("Enabled##ao", p.AmbientOcclusion);
            if (p.AmbientOcclusion)
            {
                UI::DragFloat("Radius", p.AORadius, 0.01f, 0.05f, 5.0f, "%.2f m");
                UI::SliderFloat("Intensity##ao", p.AOIntensity, 0.0f, 4.0f);
                UI::SliderFloat("Power", p.AOPower, 0.5f, 4.0f);
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Color Grading"))
        {
            UI::Checkbox("Enabled##grading", p.ColorGrading);
            if (p.ColorGrading)
            {
                UI::SliderFloat("Temperature", p.Temperature, -1.0f, 1.0f, "%+.2f");
                UI::SliderFloat("Tint##wb", p.Tint, -1.0f, 1.0f, "%+.2f");
                UI::SliderFloat("Contrast", p.Contrast, 0.0f, 2.0f);
                UI::SliderFloat("Saturation", p.Saturation, 0.0f, 2.0f);
                UI::Color3("Lift", p.Lift);
                UI::Color3("Gamma", p.Gamma);
                UI::Color3("Gain", p.Gain);
                if (ImGui::Button("Reset Grading"))
                {
                    PostProcessSettings d;
                    p.Temperature = d.Temperature; p.Tint = d.Tint; p.Contrast = d.Contrast; p.Saturation = d.Saturation;
                    p.Lift = d.Lift; p.Gamma = d.Gamma; p.Gain = d.Gain;
                }
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Lens", false))
        {
            UI::Checkbox("Vignette", p.Vignette);
            if (p.Vignette)
            {
                UI::SliderFloat("Vignette Intensity", p.VignetteIntensity, 0.0f, 1.0f);
                UI::SliderFloat("Vignette Smoothness", p.VignetteSmoothness, 0.01f, 1.0f);
                UI::Color3("Vignette Color", p.VignetteColor);
            }
            UI::Checkbox("Chromatic Aberration", p.ChromaticAberration);
            if (p.ChromaticAberration)
                UI::SliderFloat("Aberration Intensity", p.ChromaticAberrationIntensity, 0.0f, 1.0f);
            UI::Checkbox("Film Grain", p.FilmGrain);
            if (p.FilmGrain)
                UI::SliderFloat("Grain Intensity", p.FilmGrainIntensity, 0.0f, 1.0f);
            UI::EndSection();
        }

        // Unity / Unreal düzeni: Anti-Aliasing (yerel çözünürlükte yöntem), ayrı Upscaling (iç çözünürlük + yükseltici) ve ayrı
        // Sharpening. Eski sahne / betik değeri (AA listesindeki FSR, DLSS, CAS ...) yeni yapıya çevrilir
        AntiAliasing::MigrateLegacy(p);
        if (UI::BeginSection("Anti-Aliasing"))
        {
            using namespace Conqueror::AntiAliasing;
            const ModeInfo& curInfo = GetInfo(p.AntiAliasing);
            UI::BeginProperty("Method", "Anti-aliasing method at native resolution. Methods are grouped by category; greyed-out ones cannot run on this "
                                        "system (hover for the reason).");
            const std::string preview = p.AntiAliasing == AntiAliasingMode::None ? std::string("None")
                                                                                 : std::string(curInfo.Name) + " - " + curInfo.FullName;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##aa_mode", preview.c_str()))
            {
                // "None" en üstte, yöntemler kategori alt menülerinde (Unity'nin shader seçicisi gibi kısa liste)
                if (ImGui::Selectable("None", p.AntiAliasing == AntiAliasingMode::None))
                    p.AntiAliasing = AntiAliasingMode::None;
                const Group groups[] = { Group::PostProcess, Group::Multisample, Group::Supersampling, Group::Temporal, Group::VendorNative };
                for (Group g : groups)
                {
                    const bool current = p.AntiAliasing != AntiAliasingMode::None && curInfo.Category == g;
                    const std::string menuLabel = std::string(current ? "* " : "") + GetGroupName(g) + "##aa_group";
                    if (!ImGui::BeginMenu(menuLabel.c_str()))
                        continue;
                    for (int i = 1; i < GetModeCount(); ++i)
                    {
                        const ModeInfo& info = GetInfo((AntiAliasingMode)i);
                        if (info.Category != g)
                            continue;
                        const Availability av = GetAvailability(info.Mode);
                        const std::string label = std::string(info.Name) + "   " + info.FullName;
                        if (ImGui::MenuItem(label.c_str(), nullptr, p.AntiAliasing == info.Mode, av.Available))
                            p.AntiAliasing = info.Mode;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        {
                            if (av.Available)
                                ImGui::SetTooltip("%s", info.Description);
                            else
                                ImGui::SetTooltip("%s\n\nNot available on this system: %s", info.Description, av.Reason);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndCombo();
            }
            UI::EndProperty();
            const Availability curAv = GetAvailability(p.AntiAliasing);
            if (!curAv.Available)
            {
                const std::string msg = std::string(curInfo.Name) + " cannot run on this system: " + curAv.Reason + " Currently used: " +
                                        GetInfo(ResolveForThisSystem(p.AntiAliasing)).Name + ".";
                UI::InfoBox(msg.c_str(), UI::InfoType::Warning);
            }
            else
                ImGui::TextWrapped("%s", curInfo.Description);

            const AntiAliasingMode aaNow = p.AntiAliasing;
            if (aaNow == AntiAliasingMode::MSAA)
            {
                const char* msaaOpts[] = { "2x", "4x", "8x" };
                int msaaIdx = p.MSAASamples >= 8 ? 2 : p.MSAASamples >= 4 ? 1 : 0;
                if (UI::Combo("MSAA Samples", msaaIdx, msaaOpts, 3, "Hardware multisample count"))
                    p.MSAASamples = msaaIdx == 0 ? 2 : msaaIdx == 1 ? 4 : 8;
            }
            if (aaNow == AntiAliasingMode::EQAA)
            {
                const char* eqOpts[] = { "4 coverage / 2 color", "8 coverage / 4 color", "16 coverage / 8 color" };
                int eqIdx = p.EQAASamples >= 16 ? 2 : p.EQAASamples >= 8 ? 1 : 0;
                if (UI::Combo("EQAA Samples", eqIdx, eqOpts, 3, "Coverage samples / stored color samples (AMD EQAA)"))
                    p.EQAASamples = eqIdx == 0 ? 4 : eqIdx == 1 ? 8 : 16;
            }
            if (AntiAliasing::IsSingleFrame(aaNow) || aaNow == AntiAliasingMode::SMAA_T2X)
                UI::Checkbox("Temporal Stability", p.TemporalStability,
                             "Keeps thin details (gaps, distant stair edges, thin lines) from blinking while you move and smooths stair-stepped edges: "
                             "the method's result is accumulated over sub-pixel jittered frames like TAA. FXAA, SMAA, MLAA, CMAA, DEAA, GBAA and SRAA then "
                             "smooth only newly revealed pixels themselves; SMAA T2x averages only the pixels that flicker between frames.");
            if (aaNow == AntiAliasingMode::SSAA || aaNow == AntiAliasingMode::OGSSAA)
                UI::SliderFloat("SSAA Scale", p.SSAAScale, 1.25f, 4.0f, "%.2fx",
                                "Internal resolution multiplier per axis (2 = 4x samples). OGSSAA rounds it to a whole number. The cost grows with the square of the value.");
            if (aaNow == AntiAliasingMode::TAA || aaNow == AntiAliasingMode::TAA_FXAA || aaNow == AntiAliasingMode::TXAA || aaNow == AntiAliasingMode::TSAA ||
                aaNow == AntiAliasingMode::TSSAA)
            {
                UI::SliderFloat("History Weight", p.TAAFeedback, 0.5f, 0.97f, "%.2f", "Higher = smoother but more ghosting on fast motion");
                UI::SliderFloat("TAA Sharpness", p.TAASharpness, 0.0f, 1.0f, "%.2f", "Sharpening applied after the temporal resolve");
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Upscaling"))
        {
            using namespace Conqueror::AntiAliasing;
            const UpscalerInfo& upInfo = GetUpscalerInfo(p.Upscaler);
            UI::BeginProperty("Upscaler", "Renders at a lower internal resolution and upscales to the output resolution "
                                          "(Unity: Render Scale + Upscaling Filter, Unreal: Screen Percentage + upscaler).");
            const std::string preview = p.Upscaler == UpscalerMode::None ? std::string("None (Native)")
                                                                          : std::string(upInfo.Name) + " - " + upInfo.FullName;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##upscaler", preview.c_str()))
            {
                for (int i = 0; i < GetUpscalerCount(); ++i)
                {
                    const UpscalerInfo& info = GetUpscalerInfo((UpscalerMode)i);
                    const Availability av = GetUpscalerAvailability(info.Mode);
                    const std::string label = info.Mode == UpscalerMode::None ? std::string("None (Native)") : std::string(info.Name) + "   " + info.FullName;
                    if (!av.Available)
                        ImGui::BeginDisabled();
                    if (ImGui::Selectable(label.c_str(), p.Upscaler == info.Mode))
                        p.Upscaler = info.Mode;
                    if (!av.Available)
                        ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        if (av.Available)
                            ImGui::SetTooltip("%s", info.Description);
                        else
                            ImGui::SetTooltip("%s\n\nNot available on this system: %s", info.Description, av.Reason);
                    }
                    if (info.Mode == UpscalerMode::None)
                        ImGui::Separator();
                }
                ImGui::EndCombo();
            }
            UI::EndProperty();
            if (p.Upscaler != UpscalerMode::None)
            {
                const Availability upAv = GetUpscalerAvailability(p.Upscaler);
                if (!upAv.Available)
                {
                    const std::string msg = std::string(upInfo.Name) + " cannot run on this system: " + upAv.Reason + " Currently used: " +
                                            GetUpscalerInfo(ResolveUpscalerForThisSystem(p.Upscaler)).Name + ".";
                    UI::InfoBox(msg.c_str(), UI::InfoType::Warning);
                }
                else
                    ImGui::TextWrapped("%s", upInfo.Description);
                const char* qualityNames[(int)UpscaleQualityMode::Count];
                for (int i = 0; i < (int)UpscaleQualityMode::Count; ++i)
                    qualityNames[i] = GetQualityModeName((UpscaleQualityMode)i);
                int quality = std::clamp((int)p.UpscaleQuality, 0, (int)UpscaleQualityMode::Count - 1);
                if (UI::Combo("Quality Mode", quality, qualityNames, (int)UpscaleQualityMode::Count, "Internal resolution per axis (DLSS / FSR / XeSS quality modes)"))
                    p.UpscaleQuality = (UpscaleQualityMode)quality;
                if (p.UpscaleQuality == UpscaleQualityMode::Custom)
                    UI::SliderFloat("Render Scale", p.UpscaleRenderScale, 0.33f, 1.0f, "%.2f", "Internal resolution per axis (1 = native)");
                else
                    UI::Text("Render Scale", "%.0f%%", 100.0f * GetQualityModeScale(p.UpscaleQuality, p.UpscaleRenderScale));
                if (p.Upscaler == UpscalerMode::FSR1 || p.Upscaler == UpscalerMode::CAS || p.Upscaler == UpscalerMode::NIS || p.Upscaler == UpscalerMode::TAAU ||
                    p.Upscaler == UpscalerMode::FSR2)
                    UI::SliderFloat("Upscale Sharpness", p.UpscaleSharpness, 0.0f, 1.0f, "%.2f", "Sharpening done by the upscaler (FSR 1 / FSR 2 RCAS, CAS, NIS, TAAU)");
                if (p.Upscaler == UpscalerMode::TAAU)
                    UI::SliderFloat("History Weight", p.TAAFeedback, 0.5f, 0.97f, "%.2f", "TAAU history weight: higher = smoother but more ghosting on fast motion");
                if (p.Upscaler == UpscalerMode::NIS && GetQualityModeScale(p.UpscaleQuality, p.UpscaleRenderScale) < 0.5f)
                    UI::InfoBox("NIS upscales at most 2x per axis; the internal resolution is limited to 50%.", UI::InfoType::Info);
                if (IsSupersampling(p.AntiAliasing))
                    UI::InfoBox("Upscaling is off while a supersampling anti-aliasing method renders above native resolution.", UI::InfoType::Warning);
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Sharpening"))
        {
            UI::Checkbox("Sharpen", p.Sharpen, "Sharpens the final image");
            if (p.Sharpen)
            {
                const char* methods[] = { "Edge-Aware", "CAS (AMD FidelityFX)" };
                int method = std::clamp((int)p.SharpenMode, 0, 1);
                if (UI::Combo("Sharpen Method", method, methods, 2,
                              "Edge-Aware: unsharp mask that leaves high-contrast edges alone. CAS: AMD Contrast Adaptive Sharpening (stronger on low-contrast detail)."))
                    p.SharpenMode = (SharpenMethod)method;
                UI::SliderFloat("Sharpen Amount", p.SharpenAmount, 0.0f, 1.0f);
            }
            UI::EndSection();
        }

        ImGui::EndDisabled();
    }

    // ===================================================================== Lightmaps sekmesi

    void LightingPanel::DrawLightmapsTab()
    {
        auto drawInfo = [](const BakeResultInfo& info)
        {
            UI::Text("Atlas Size", "%d x %d", info.AtlasWidth, info.AtlasHeight);
            if (info.Texels > 0) UI::Text("Texels", "%d", info.Texels);
            if (info.Objects > 0) UI::Text("Objects", "%d", info.Objects);
            if (info.Seconds > 0.0f) UI::Text("Bake Time", "%.2f s", info.Seconds);
            UI::Text("Generated", "%s", info.When.c_str());
        };
        auto drawPreview = [](const std::shared_ptr<Texture2D>& tex)
        {
            if (!tex || !tex->GetRendererID())
                return;
            float w = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
            float aspect = tex->GetHeight() > 0 ? (float)tex->GetHeight() / (float)tex->GetWidth() : 1.0f;
            ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), ImVec2(w, w * aspect), ImVec2(0, 1), ImVec2(1, 0));
        };

        if (UI::BeginSection("Baked Lightmaps"))
        {
            auto tex = m_Context->GetBakedLightmapTexture();
            if (m_Bake.Active && m_Bake.Kind == BakeKind::BakedLightmap)
                UI::InfoBox("Baking...", UI::InfoType::Info);
            else if (tex)
            {
                UI::Badge("BAKED", UI::Colors::Success);
                drawInfo(m_BakedInfo);
                if (!m_Context->GetBakedLightmapPath().empty())
                    UI::Text("File", "%s", m_Context->GetBakedLightmapPath().c_str());
                drawPreview(tex);
                if (UI::DangerButton("Clear Baked Lightmap"))
                {
                    m_Context->SetBakedLightmapTexture(nullptr);
                    m_Context->SetBakedLightmapPath("");
                    m_BakedInfo = {};
                }
            }
            else
            {
                UI::InfoBox("No baked lightmap. Mark objects as Static and press Generate Lighting.", UI::InfoType::Info);
                if (UI::AccentButton("Bake Lightmaps Only") && !m_Bake.Active)
                    StartBake(BakeKind::BakedLightmap);
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Realtime Lightmaps"))
        {
            if (!m_Context->GetRenderSettings().Bake.RealtimeGI)
                UI::InfoBox("Realtime Global Illumination is disabled (Scene tab).", UI::InfoType::Info);
            if (m_RealtimeLightmap)
            {
                UI::Badge("REALTIME", UI::Colors::Accent);
                drawInfo(m_RealtimeInfo);
                drawPreview(m_RealtimeLightmap);
            }
            if (ImGui::Button("Update Realtime Lightmaps") && !m_Bake.Active)
                StartBake(BakeKind::RealtimeLightmap);
            UI::EndSection();
        }
    }

    // ===================================================================== Probes sekmesi

    void LightingPanel::DrawProbesTab()
    {
        auto& b = m_Context->GetRenderSettings().Bake;

        if (UI::BeginSection("Adaptive Probe Volume"))
        {
            auto apv = m_Context->GetAdaptiveProbeVolume();
            if (apv && apv->IsBaked())
            {
                UI::Badge("BAKED", UI::Colors::Success);
                UI::Text("Probes", "%d", apv->GetTotalProbeCount());
                UI::Text("Levels", "%d", apv->GetNumLevels());
                if (m_APVInfo.Seconds > 0.0f)
                    UI::Text("Bake Time", "%.2f s", m_APVInfo.Seconds);
                if (UI::DangerButton("Clear APV Data"))
                    m_Context->SetAdaptiveProbeVolume(nullptr);
            }
            else
            {
                UI::InfoBox("Probes store indirect light (L2 spherical harmonics) for dynamic objects.", UI::InfoType::Info);
            }

            UI::Checkbox("Fit Volume To Scene", b.APVFitToScene, "Automatically sizes the volume around all static renderers.");
            if (!b.APVFitToScene)
            {
                UI::Vec3("Volume Origin", b.APVOrigin, 0.1f);
                UI::Vec3("Volume Size", b.APVSize, 0.1f, 10.0f);
            }
            UI::Vec3("Probe Offset", b.APVProbeOffset, 0.05f);
            UI::DragFloat("Min Probe Spacing", b.APVMinSpacing, 0.05f, 0.1f, 50.0f, "%.2f m");
            UI::DragFloat("Max Probe Spacing", b.APVMaxSpacing, 0.1f, b.APVMinSpacing, 250.0f, "%.2f m");
            const char* modes[] = { "Single Scene", "Baking Set" };
            UI::Combo("Baking Mode", b.APVBakingMode, modes, 2);
            UI::SubHeader("Probe Invalidity");
            UI::Checkbox("Dilation", b.APVDilation);
            UI::Checkbox("Virtual Offset", b.APVVirtualOffset);
            if (b.APVVirtualOffset)
            {
                UI::SliderFloat("Validity Threshold", b.APVValidityThreshold, 0.0f, 1.0f);
                UI::SliderFloat("Search Distance", b.APVSearchDistanceMultiplier, 0.0f, 1.0f);
                UI::DragFloat("Geometry Bias", b.APVGeometryBias, 0.001f, 0.0f, 0.5f, "%.3f");
                UI::DragFloat("Ray Origin Bias", b.APVRayOriginBias, 0.0005f, -0.1f, 0.0f, "%.4f");
            }
            ImGui::Spacing();
            if (UI::AccentButton("Bake Probe Volume", ImVec2(-FLT_MIN, 0)) && !m_Bake.Active)
            {
                m_APVVolumeRequested = true;
                StartBake(BakeKind::APV);
                m_APVVolumeRequested = false;
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Reflection Probes"))
        {
            auto view = m_Context->m_Registry.view<TagComponent, ReflectionProbeComponent>();
            int count = 0;
            if (ImGui::BeginTable("##probes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Probe", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();
                for (auto e : view)
                {
                    ++count;
                    auto& tag = view.get<TagComponent>(e);
                    auto& probe = view.get<ReflectionProbeComponent>(e);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID((int)(uint32_t)e);
                    if (ImGui::Selectable(tag.Tag.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && m_SelectCallback)
                        m_SelectCallback(Entity{ e, m_Context.get() });
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", probe.Resolution);
                    ImGui::TableNextColumn();
                    if (probe.NeedsCapture)
                        UI::Badge("PENDING", UI::Colors::Warning);
                    else if (probe.Captured)
                        UI::Badge(probe.RealtimeUpdate ? "REALTIME" : "CAPTURED", UI::Colors::Success);
                    else
                        UI::Badge("NONE", UI::Colors::TextDim);
                }
                ImGui::EndTable();
            }
            if (count == 0)
                UI::InfoBox("No reflection probes. Add one with GameObject > Light > Reflection Probe for accurate local reflections.", UI::InfoType::Info);
            else if (ImGui::Button("Bake All Reflection Probes", ImVec2(-FLT_MIN, 0)))
                m_Context->InvalidateReflectionProbes();
            UI::EndSection();
        }
    }

    // ===================================================================== Light Explorer

    void LightingPanel::DrawLightExplorerTab()
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##lightfilter", "Filter lights by name...", m_LightFilter, sizeof(m_LightFilter));

        auto passes = [&](const std::string& name)
        {
            if (!m_LightFilter[0])
                return true;
            std::string a = name, f = m_LightFilter;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(f.begin(), f.end(), f.begin(), ::tolower);
            return a.find(f) != std::string::npos;
        };

        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable("##lights", 8, flags))
            return;
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 26.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Intensity", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Shadows", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        const char* modes[] = { "Realtime", "Mixed", "Baked" };
        int rows = 0;
        auto rowCommon = [&](entt::entity e, const char* type, LightMode& mode, glm::vec3& color, float& intensity, float* range, bool& shadows)
        {
            Entity entity{ e, m_Context.get() };
            std::string name = entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().Tag : "Light";
            if (!passes(name))
                return;
            ++rows;
            ImGui::TableNextRow();
            ImGui::PushID((int)(uint32_t)e);
            ImGui::TableNextColumn();
            bool active = m_Context->IsEntityActive(e);
            if (ImGui::Checkbox("##on", &active))
            {
                if (active && entity.HasComponent<ActiveComponent>())
                    entity.RemoveComponent<ActiveComponent>();
                else if (!active && !entity.HasComponent<ActiveComponent>())
                    entity.AddComponent<ActiveComponent>();
            }
            ImGui::TableNextColumn();
            if (ImGui::Selectable(name.c_str(), false) && m_SelectCallback)
                m_SelectCallback(entity);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(type);
            ImGui::TableNextColumn();
            int m = (int)mode;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##mode", &m, modes, 3))
                mode = (LightMode)m;
            ImGui::TableNextColumn();
            ImGui::ColorEdit3("##color", glm::value_ptr(color), ImGuiColorEditFlags_NoInputs);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat("##intensity", &intensity, 0.01f, 0.0f, 1000.0f, "%.2f");
            ImGui::TableNextColumn();
            if (range)
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##range", range, 0.05f, 0.01f, 10000.0f, "%.1f");
            }
            else
            {
                ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            ImGui::Checkbox("##shadows", &shadows);
            ImGui::PopID();
        };

        auto& reg = m_Context->m_Registry;
        for (auto e : reg.view<DirectionalLightComponent>())
        {
            auto& l = reg.get<DirectionalLightComponent>(e);
            rowCommon(e, "Directional", l.Mode, l.Color, l.Intensity, nullptr, l.CastShadows);
        }
        for (auto e : reg.view<PointLightComponent>())
        {
            auto& l = reg.get<PointLightComponent>(e);
            rowCommon(e, "Point", l.Mode, l.Color, l.Intensity, &l.Range, l.CastShadows);
        }
        for (auto e : reg.view<SpotLightComponent>())
        {
            auto& l = reg.get<SpotLightComponent>(e);
            rowCommon(e, "Spot", l.Mode, l.Color, l.Intensity, &l.Range, l.CastShadows);
        }
        ImGui::EndTable();

        if (rows == 0)
            UI::InfoBox(m_LightFilter[0] ? "No light matches the filter." : "The scene has no lights.", UI::InfoType::Info);
        ImGui::TextDisabled("Realtime point/spot lights: up to 64 / 32 per view. Shadows: sun (cascaded).");
    }

    // ===================================================================== Bake motoru

    LightmapSettings LightingPanel::MakeLightmapSettings(bool realtime) const
    {
        const auto& b = m_Context->GetRenderSettings().Bake;
        LightmapSettings s;
        s.Lightmapper = b.Lightmapper;
        s.ImportanceSampling = b.ImportanceSampling;
        s.Resolution = realtime ? b.RealtimeResolution : b.Resolution;
        s.MaxLightmapSize = b.MaxLightmapSize;
        s.MaxBounces = realtime ? b.RealtimeMaxBounces : b.MaxBounces;
        s.DirectSamples = b.DirectSamples;
        s.IndirectSamples = realtime ? b.RealtimeIndirectSamples : b.IndirectSamples;
        s.EnvironmentSamples = b.EnvironmentSamples;
        s.Filtering = b.Filtering;
        s.Packing = b.Packing;
        s.UseMipmapLimits = b.UseMipmapLimits;
        s.Compression = b.Compression;
        s.AmbientOcclusion = b.AmbientOcclusion;
        s.DirectionalMode = b.DirectionalMode;
        s.AlbedoBoost = b.AlbedoBoost;
        s.IndirectIntensity = b.IndirectIntensity;
        return s;
    }

    std::shared_ptr<Scene> LightingPanel::CreateSnapshot() const
    {
        // Bake thread'i canlı sahneye asla dokunmaz: ana thread'de bir kopya alınır
        auto snapshot = std::make_shared<Scene>();
        snapshot->CloneSceneFrom(m_Context);
        snapshot->UpdateWorldTransforms();
        return snapshot;
    }

    void LightingPanel::GenerateLighting()
    {
        if (!m_Context || m_Bake.Active)
            return;
        auto& b = m_Context->GetRenderSettings().Bake;
        // Sıra: lightmap (BakedGI) -> APV (bileşen ya da ayar) -> yansıma probları
        m_Context->InvalidateReflectionProbes();
        if (b.BakedGI && b.Lightmapper != 0)
            StartBake(BakeKind::BakedLightmap);
        else if (!m_Context->m_Registry.view<AdaptiveProbeVolumeComponent>().empty() || LightProbeGroups::Any(*m_Context))
            StartBake(BakeKind::APV);
        else
        {
            m_StatusMessage = "Reflection probes queued for capture.";
            m_StatusTime = std::chrono::steady_clock::now();
        }
    }

    void LightingPanel::ClearBakedData()
    {
        if (!m_Context)
            return;
        CancelBake();
        JoinWorker();
        m_Context->SetBakedLightmapTexture(nullptr);
        m_Context->SetBakedLightmapPath("");
        m_Context->SetAdaptiveProbeVolume(nullptr);
        Renderer3D::SetLightmap(nullptr);
        Renderer3D::SetAdaptiveProbeVolume(nullptr);
        m_RealtimeLightmap.reset();
        m_BakedInfo = {};
        m_RealtimeInfo = {};
        m_APVInfo = {};
        m_StatusMessage = "Baked lighting data cleared.";
        m_StatusTime = std::chrono::steady_clock::now();
    }

    void LightingPanel::StartBake(BakeKind kind, bool silent)
    {
        if (!m_Context || m_Bake.Active)
            return;
        JoinWorker();

        m_Bake.Active = true;
        m_Bake.Kind = kind;
        m_Bake.Silent = silent;
        m_Bake.Done.store(false);
        m_Bake.Progress.store(0.0f);
        {
            std::lock_guard<std::mutex> lock(m_Bake.StepMutex);
            m_Bake.Step = "Preparing scene snapshot...";
        }
        m_Bake.Start = std::chrono::steady_clock::now();
        m_Bake.Snapshot = CreateSnapshot();
        m_Bake.Baker.reset();
        m_Bake.APV.reset();

        auto progressCallback = [this](float progress, const std::string& step)
        {
            m_Bake.Progress.store(progress);
            std::lock_guard<std::mutex> lock(m_Bake.StepMutex);
            m_Bake.Step = step;
        };

        Scene* snapshot = m_Bake.Snapshot.get();
        if (kind == BakeKind::APV)
        {
            const auto& b = m_Context->GetRenderSettings().Bake;
            AdaptiveProbeVolumeSettings settings;
            glm::vec3 origin = b.APVOrigin, size = b.APVSize;
            if (b.APVFitToScene)
            {
                glm::vec3 mn(1e9f), mx(-1e9f);
                bool any = false;
                auto view = snapshot->m_Registry.view<TransformComponent, MeshRendererComponent>();
                for (auto e : view)
                {
                    auto& tc = view.get<TransformComponent>(e);
                    glm::vec3 p = glm::vec3(tc.WorldTransform[3]);
                    glm::vec3 ext = glm::abs(glm::vec3(glm::length(glm::vec3(tc.WorldTransform[0])),
                                                       glm::length(glm::vec3(tc.WorldTransform[1])),
                                                       glm::length(glm::vec3(tc.WorldTransform[2])))) * 0.5f;
                    mn = glm::min(mn, p - ext);
                    mx = glm::max(mx, p + ext);
                    any = true;
                }
                if (any)
                {
                    origin = mn - glm::vec3(1.0f);
                    size = (mx - mn) + glm::vec3(2.0f);
                }
            }
            settings.Origin = origin;
            settings.Size = glm::max(size, glm::vec3(0.5f));
            settings.ProbeOffset = b.APVProbeOffset;
            settings.MinSpacing = b.APVMinSpacing;
            settings.MaxSpacing = b.APVMaxSpacing;
            settings.BakingMode = b.APVBakingMode;
            // L2 SH düşük frekanslı: yüz başına 16x16 (1536 yön) yeterli, 32x32'den 4 kat hızlı
            settings.ProbeResolution = 16;
            // Gökyüzü / ortam ışığı: son çizilen karedeki ortamla aynı (CQ_AmbientDiffuse); problar ortam ışığının yerine geçer
            {
                const SceneEnvironment& env = Renderer3D::GetEnvironment();
                auto envMap = Renderer3D::GetActiveEnvironmentMap();
                APVSkyModel& sky = settings.Sky;
                sky.Source = env.AmbientSource;
                sky.HasSH = envMap && env.Settings.Environment.ImageBasedLighting;
                if (sky.HasSH)
                {
                    const auto& sh = envMap->GetIrradianceSH();
                    for (int i = 0; i < 9; ++i)
                        sky.SH[i] = sh[i];
                }
                sky.Sky = env.AmbientSource == 2 ? env.SkyColor : env.AmbientColor;
                sky.Equator = env.EquatorColor;
                sky.Ground = env.GroundColor;
                sky.Intensity = env.AmbientIntensity * (env.AmbientSource == 0 ? env.Settings.Environment.SkyboxIntensity : 1.0f);
                if (sky.HasSH && env.Skybox)
                    sky.Intensity *= env.SkyboxExposure;
                const float rot = glm::radians(env.Skybox ? env.SkyboxRotation : 0.0f);
                sky.RotCos = std::cos(rot);
                sky.RotSin = std::sin(rot);
                CQ_CORE_INFO("APV bake sky: source {0}, SH {1} (L00 {2:.3f} {3:.3f} {4:.3f}), color {5:.3f} {6:.3f} {7:.3f}, intensity {8:.3f}",
                             sky.Source, sky.HasSH, sky.SH[0].x, sky.SH[0].y, sky.SH[0].z, sky.Sky.x, sky.Sky.y, sky.Sky.z, sky.Intensity);
            }
            ProbeInvaliditySettings inv;
            inv.Dilation = b.APVDilation;
            inv.VirtualOffset = b.APVVirtualOffset;
            inv.ValidityThreshold = b.APVValidityThreshold;
            inv.SearchDistanceMultiplier = b.APVSearchDistanceMultiplier;
            inv.GeometryBias = b.APVGeometryBias;
            inv.RayOriginBias = b.APVRayOriginBias;
            // Sahnede Adaptive Probe Volume bileşeni varsa (Unity'deki Probe Volume gibi) pişirme hacmi bileşenlerin kutularının birleşimidir
            // (konum = merkez, ölçek = boyut); aralık ve geçersizlik ayarları ilk bileşenden
            {
                auto apvView = snapshot->m_Registry.view<TransformComponent, AdaptiveProbeVolumeComponent>();
                bool first = true;
                glm::vec3 mn(0.0f), mx(0.0f);
                for (auto e : apvView)
                {
                    const auto& tc = apvView.get<TransformComponent>(e);
                    const auto& vc = apvView.get<AdaptiveProbeVolumeComponent>(e);
                    const glm::vec3 c = glm::vec3(tc.WorldTransform[3]);
                    const glm::vec3 half = 0.5f * glm::vec3(glm::length(glm::vec3(tc.WorldTransform[0])), glm::length(glm::vec3(tc.WorldTransform[1])),
                                                            glm::length(glm::vec3(tc.WorldTransform[2])));
                    if (first)
                    {
                        mn = c - half;
                        mx = c + half;
                        settings.ProbeOffset = vc.ProbeOffset;
                        settings.MinSpacing = std::max(vc.MinSpacing, 0.1f);
                        settings.MaxSpacing = std::max(vc.MaxSpacing, settings.MinSpacing);
                        settings.BakingMode = vc.BakingMode;
                        inv.Dilation = vc.Dilation;
                        inv.VirtualOffset = vc.VirtualOffset;
                        inv.ValidityThreshold = vc.ValidityThreshold;
                        inv.SearchDistanceMultiplier = vc.SearchDistanceMultiplier;
                        inv.GeometryBias = vc.GeometryBias;
                        inv.RayOriginBias = vc.RayOriginBias;
                        first = false;
                    }
                    else
                    {
                        mn = glm::min(mn, c - half);
                        mx = glm::max(mx, c + half);
                    }
                }
                if (!first)
                {
                    settings.Origin = mn;
                    settings.Size = glm::max(mx - mn, glm::vec3(0.5f));
                    CQ_CORE_INFO("APV bake: volume from Adaptive Probe Volume components, origin ({:.2f}, {:.2f}, {:.2f}) size ({:.2f}, {:.2f}, {:.2f})",
                                 mn.x, mn.y, mn.z, settings.Size.x, settings.Size.y, settings.Size.z);
                }
            }
            // Light Probe Group probları (Unity): elle yerleştirilmiş, her biri olduğu yerde pişer. Hacim ızgarası yalnızca Adaptive Probe
            // Volume bileşeni varsa ya da Bake Probe Volume düğmesiyle istenince yerleştirilir
            LightProbeGroups::CollectWorldProbes(*snapshot, settings.ExplicitProbes);
            if (settings.ExplicitProbes.size() > (size_t)APV_MAX_PROBES)
            {
                CQ_CORE_WARN("Light Probe Groups: {0} probes, only the first {1} are used", settings.ExplicitProbes.size(), APV_MAX_PROBES);
                settings.ExplicitProbes.resize(APV_MAX_PROBES);
            }
            settings.PlaceVolumeProbes = m_APVVolumeRequested || !snapshot->m_Registry.view<AdaptiveProbeVolumeComponent>().empty();
            if (!settings.ExplicitProbes.empty())
                CQ_CORE_INFO("APV bake: {0} Light Probe Group probes{1}", settings.ExplicitProbes.size(),
                             settings.PlaceVolumeProbes ? " + volume grid" : "");
            m_Bake.APV = AdaptiveProbeVolume::Create(settings);
            m_Bake.APV->SetInvaliditySettings(inv);
            m_Bake.APV->SetProgressCallback(progressCallback);
            auto* apv = m_Bake.APV.get();
            auto* done = &m_Bake.Done;
            m_Bake.Worker = std::thread([apv, snapshot, done]()
            {
                try { apv->Bake(snapshot); }
                catch (const std::exception& e) { CQ_CORE_ERROR("APV bake failed: {0}", e.what()); }
                done->store(true);
            });
        }
        else
        {
            bool realtime = kind == BakeKind::RealtimeLightmap;
            m_Bake.Baker = LightmapBaker::Create(MakeLightmapSettings(realtime));
            m_Bake.Baker->SetRealtimeMode(realtime);
            m_Bake.Baker->SetProgressCallback(progressCallback);
            auto* baker = m_Bake.Baker.get();
            auto* done = &m_Bake.Done;
            m_Bake.Worker = std::thread([baker, snapshot, done]()
            {
                try { baker->Bake(snapshot); }
                catch (const std::exception& e) { CQ_CORE_ERROR("Lightmap bake failed: {0}", e.what()); }
                done->store(true);
            });
        }
    }

    void LightingPanel::CancelBake()
    {
        if (!m_Bake.Active)
            return;
        if (m_Bake.Baker) m_Bake.Baker->RequestCancel();
        if (m_Bake.APV) m_Bake.APV->RequestCancel();
    }

    void LightingPanel::JoinWorker()
    {
        if (m_Bake.Worker.joinable())
            m_Bake.Worker.join();
    }

    void LightingPanel::PollBake()
    {
        if (m_Bake.Active && m_Bake.Done.load())
            FinishBake();
    }

    void LightingPanel::FinishBake()
    {
        JoinWorker();
        float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_Bake.Start).count();
        BakeKind kind = m_Bake.Kind;
        bool silent = m_Bake.Silent;
        m_Bake.Active = false;
        m_Bake.Kind = BakeKind::None;

        if (!m_Context)
        {
            m_Bake.Snapshot.reset();
            return;
        }

        if (kind == BakeKind::APV && m_Bake.APV)
        {
            if (m_Bake.APV->IsBaked())
            {
                m_Context->SetAdaptiveProbeVolume(m_Bake.APV);
                m_APVInfo.Valid = true;
                m_APVInfo.Seconds = seconds;
                m_APVInfo.When = NowString();
                m_StatusMessage = "Probe volume baked: " + std::to_string(m_Bake.APV->GetTotalProbeCount()) + " probes.";
            }
            else
            {
                m_StatusMessage = m_Bake.APV->WasCancelled() ? "Probe volume bake cancelled." : "Probe volume bake failed (see Log).";
            }
        }
        else if (m_Bake.Baker)
        {
            auto& baker = *m_Bake.Baker;
            const auto& atlas = baker.GetAtlas();
            const bool cancelled = baker.WasCancelled();
            if (cancelled || atlas.Width == 0 || atlas.Texels.empty())
            {
                m_StatusMessage = cancelled ? "Lightmap bake cancelled." : "Nothing to bake: no static geometry.";
            }
            else
            {
                baker.ApplyUV2ToMeshes();
                auto texture = baker.CreateLightmapTexture();
                BakeResultInfo info;
                info.Valid = texture != nullptr;
                info.AtlasWidth = (int)atlas.Width;
                info.AtlasHeight = (int)atlas.Height;
                info.Texels = (int)atlas.Texels.size();
                info.Seconds = seconds;
                info.When = NowString();
                auto& reg = m_Context->m_Registry;
                for (auto e : reg.view<TagComponent, MeshRendererComponent>())
                    if (reg.get<TagComponent>(e).IsStatic) ++info.Objects;

                if (kind == BakeKind::RealtimeLightmap)
                {
                    m_RealtimeLightmap = texture;
                    m_RealtimeInfo = info;
                    if (texture && !m_Context->GetBakedLightmapTexture())
                        m_Context->SetBakedLightmapTexture(texture);
                    m_StatusMessage = "Realtime lightmaps updated.";
                }
                else
                {
                    m_BakedInfo = info;
                    m_Context->SetBakedLightmapTexture(texture);
                    auto projectDir = Project::GetActiveProjectDirectory();
                    if (!projectDir.empty() && texture)
                    {
                        std::error_code ec;
                        auto dir = projectDir / "Assets" / "Lightmaps";
                        std::filesystem::create_directories(dir, ec);
                        auto file = dir / "baked_lightmap.png";
                        if (baker.SaveToFile(file.string()))
                            m_Context->SetBakedLightmapPath("Assets/Lightmaps/baked_lightmap.png");
                    }
                    m_StatusMessage = "Lightmaps baked in " + std::to_string((int)(seconds * 10.0f) / 10.0f).substr(0, 4) + " s.";
                }
            }
            // Zincir: APV bileşeni varsa onu da pişir; lightmap'lenecek statik nesne olmasa da (Unity gibi: problar ışıkları / gökyüzünü yakalar)
            if (kind == BakeKind::BakedLightmap && !cancelled && !silent &&
                (!m_Context->m_Registry.view<AdaptiveProbeVolumeComponent>().empty() || LightProbeGroups::Any(*m_Context)))
            {
                m_Bake.Snapshot.reset();
                m_StatusTime = std::chrono::steady_clock::now();
                StartBake(BakeKind::APV, silent);
                return;
            }
        }

        m_StatusTime = std::chrono::steady_clock::now();
        m_Bake.Snapshot.reset();
        m_Bake.Baker.reset();
        m_LastLightingHash = ComputeLightingHash();
    }

    uint64_t LightingPanel::ComputeLightingHash() const
    {
        uint64_t h = 1469598103934665603ull;
        auto& reg = m_Context->m_Registry;
        for (auto e : reg.view<DirectionalLightComponent>())
        {
            auto& l = reg.get<DirectionalLightComponent>(e);
            HashVec3(h, l.Direction); HashVec3(h, l.Color); HashFloat(h, l.Intensity);
            HashCombine(h, (uint64_t)l.Mode);
        }
        for (auto e : reg.view<TransformComponent, PointLightComponent>())
        {
            auto& l = reg.get<PointLightComponent>(e);
            HashVec3(h, glm::vec3(reg.get<TransformComponent>(e).WorldTransform[3]));
            HashVec3(h, l.Color); HashFloat(h, l.Intensity); HashFloat(h, l.Range);
        }
        for (auto e : reg.view<TransformComponent, SpotLightComponent>())
        {
            auto& l = reg.get<SpotLightComponent>(e);
            HashVec3(h, glm::vec3(reg.get<TransformComponent>(e).WorldTransform[3]));
            HashVec3(h, l.Direction); HashVec3(h, l.Color); HashFloat(h, l.Intensity); HashFloat(h, l.Range);
        }
        for (auto e : reg.view<TagComponent, TransformComponent, MeshRendererComponent>())
        {
            if (!reg.get<TagComponent>(e).IsStatic)
                continue;
            const auto& wt = reg.get<TransformComponent>(e).WorldTransform;
            for (int c = 0; c < 4; ++c)
                HashVec3(h, glm::vec3(wt[c]));
            HashVec3(h, MeshRendererMaterial::Shared(reg.get<MeshRendererComponent>(e)).Albedo);
        }
        return h;
    }

    void LightingPanel::UpdateAutoGenerate()
    {
        if (!m_Context)
            return;
        auto& b = m_Context->GetRenderSettings().Bake;
        if (!b.AutoGenerate && !b.RealtimeGI)
            return;

        uint64_t hash = ComputeLightingHash();
        auto now = std::chrono::steady_clock::now();
        if (m_LastLightingHash == 0)
        {
            m_LastLightingHash = hash;
            return;
        }
        if (hash != m_LastLightingHash)
        {
            m_LastLightingHash = hash;
            m_AutoPending = true;
            m_AutoChangeTime = now;
        }
        // Son değişiklikten 1 sn sonra (sürüklerken her karede pişirme)
        if (m_AutoPending && !m_Bake.Active && std::chrono::duration<float>(now - m_AutoChangeTime).count() > 1.0f)
        {
            m_AutoPending = false;
            if (b.AutoGenerate && b.BakedGI && b.Lightmapper != 0)
                StartBake(BakeKind::BakedLightmap, true);
            else if (b.RealtimeGI)
                StartBake(BakeKind::RealtimeLightmap, true);
        }
    }
}
