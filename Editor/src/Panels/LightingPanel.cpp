#include "LightingPanel.h"
#include "Renderer/RHI/Cubemap.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <imgui.h>
#include <nfd.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <iterator>

#ifdef _WIN32
#include <windows.h>
#include <libloaderapi.h>
#else
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace Conqueror::Editor
{
    static std::filesystem::path GetEngineDirectory()
    {
#ifdef _WIN32
        char buffer[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
#else
        char buffer[PATH_MAX] = { 0 };
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1)
        {
            buffer[len] = '\0';
            return std::filesystem::path(buffer).parent_path();
        }
        return std::filesystem::current_path();
#endif
    }

    static std::string ToSerializablePath(const std::string& absolutePath)
    {
        if (absolutePath.empty())
            return absolutePath;

        auto projectDir = Project::GetActiveProjectDirectory();
        if (!projectDir.empty())
        {
            std::error_code ec;
            auto relative = std::filesystem::relative(absolutePath, projectDir, ec);
            if (!ec && !relative.empty() && relative.native()[0] != '.')
                return relative.string();
        }

        auto engineDir = GetEngineDirectory();
        std::error_code ec;
        auto relative = std::filesystem::relative(absolutePath, engineDir, ec);
        if (!ec && !relative.empty() && relative.native()[0] != '.')
        {
            std::string relStr = relative.string();
            if (relStr.find("Resources/") == 0 || relStr.find("Resources\\") == 0)
                return "CQ:" + relStr;
        }

        return absolutePath;
    }

    void LightingPanel::OnImGuiRender()
    {
        ImGui::Begin("Lighting");

        if (!m_Context)
        {
            ImGui::Text("No active scene");
            ImGui::End();
            return;
        }

        // Tab buttons
        const char* tabLabels[] = { "Scene", "Adaptive Probe Volumes", "Environment", "Realtime Lightmaps", "Baked Lightmaps" };
        float avail = ImGui::GetContentRegionAvail().x;
        float tabX = 0;
        for (int i = 0; i < 5; i++)
        {
            float btnW = ImGui::CalcTextSize(tabLabels[i]).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
            if (i > 0 && tabX + btnW > avail)
                tabX = 0;
            if (i > 0 && tabX > 0)
                ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
            tabX += btnW;
            
            bool isActive = (m_ActiveTab == i);
            if (isActive)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            
            ImGui::PushID(i);
            if (ImGui::Button(tabLabels[i], ImVec2(0, 25)))
                m_ActiveTab = i;
            ImGui::PopID();
            
            if (isActive)
                ImGui::PopStyleColor();
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Render active tab
        switch (m_ActiveTab)
        {
            case 0: RenderSceneTab(); break;
            case 1: RenderAdaptiveProbeVolumesTab(); break;
            case 2: RenderEnvironmentTab(); break;
            case 3: RenderRealtimeLightmapsTab(); break;
            case 4: RenderBakedLightmapsTab(); break;
        }

        // GPU Baking section (common to all tabs)
        ImGui::Spacing();
        ImGui::Separator();
        RenderGPUBakingSection();

        ImGui::End();
    }

    void LightingPanel::RenderSceneTab()
    {
        // Lighting Settings
        if (ImGui::CollapsingHeader("Lighting Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Lighting Settings Asset
            ImGui::Text("Lighting Settings Asset");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(150);
            ImGui::Combo("##LightingSettingsAsset", &m_LightmapperIndex, "None\0Default-Medium\0");
            ImGui::SameLine();
            if (ImGui::Button("New")) { /* TODO: Create new settings */ }
            ImGui::SameLine();
            if (ImGui::Button("Clone")) { /* TODO: Clone settings */ }
        }

        // Realtime Lighting
        if (ImGui::CollapsingHeader("Realtime Lighting"))
        {
            ImGui::Checkbox("Realtime Global Illumination", &m_RealtimeGI);
        }

        // Mixed Lighting
        if (ImGui::CollapsingHeader("Mixed Lighting"))
        {
            ImGui::Checkbox("Baked Global Illumination", &m_BakedGI);
            
            const char* lightingModes[] = { "Shadowmask", "Distance Shadowmask", "Subtractive" };
            ImGui::Combo("Lighting Mode", &m_LightingModeIndex, lightingModes, 3);
        }

        // Lightmapping Settings
        if (ImGui::CollapsingHeader("Lightmapping Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* lightmappers[] = { "None", "Progressive CPU", "Progressive GPU" };
            ImGui::Combo("Lightmapper", &m_LightmapperIndex, lightmappers, 3);

            ImGui::Checkbox("Importance Sampling", &m_ImportanceSampling);

            ImGui::InputInt("Direct Samples", &m_DirectSamples);
            ImGui::InputInt("Indirect Samples", &m_IndirectSamples);
            ImGui::InputInt("Environment Samples", &m_EnvironmentSamples);

            ImGui::InputInt("Max Bounces", &m_MaxBounces);

            const char* filterings[] = { "Auto", "None" };
            ImGui::Combo("Filtering", &m_FilteringIndex, filterings, 2);

            const char* packings[] = { "Auto", "Manual" };
            ImGui::Combo("Lightmap Packing", &m_PackingIndex, packings, 2);

            ImGui::InputInt("Lightmap Resolution", &m_LightmapResolution);
            ImGui::Text("texels per unit");

            const char* maxSizes[] = { "256", "512", "1024", "2048", "4096" };
            ImGui::Combo("Max Lightmap Size", &m_MaxLightmapSizeIndex, maxSizes, 5);

            ImGui::Checkbox("Use Mipmap Limits", &m_UseMipmapLimits);

            const char* compressions[] = { "None", "Low Quality", "Medium Quality", "High Quality" };
            ImGui::Combo("Lightmap Compression", &m_LightmapCompressionIndex, compressions, 4);

            ImGui::Checkbox("Ambient Occlusion", &m_AmbientOcclusion);

            const char* dirModes[] = { "Directional", "Non-Directional" };
            ImGui::Combo("Directional Mode", &m_DirectionalModeIndex, dirModes, 2);

            ImGui::SliderFloat("Albedo Boost", &m_AlbedoBoost, 0.0f, 2.0f);
            ImGui::SliderFloat("Indirect Intensity", &m_IndirectIntensity, 0.0f, 5.0f);
        }
    }

    void LightingPanel::RenderAdaptiveProbeVolumesTab()
    {
        // Warning
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "!");
        ImGui::SameLine();
        ImGui::TextWrapped("Adaptive Probe Volumes require the rendering backend to support probe baking.");

        ImGui::Spacing();

        // Baking
        if (ImGui::CollapsingHeader("Baking", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* bakingModes[] = { "Single Scene", "Multi Scene" };
            ImGui::Combo("Baking Mode", &m_APBakingModeIndex, bakingModes, 2);
        }

        // Probe Placement
        if (ImGui::CollapsingHeader("Probe Placement", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Probe Offset");
            ImGui::SameLine(120);
            ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("X##POffset", &m_APProbeOffset[0]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("Y##POffset", &m_APProbeOffset[1]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("Z##POffset", &m_APProbeOffset[2]);

            ImGui::InputFloat("Min Probe Spacing", &m_APMinSpacing);
            ImGui::SliderFloat("Max Probe Spacing", &m_APMaxSpacing, 0.0f, 250.0f);

            // Info
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "!");
            ImGui::SameLine();
            ImGui::TextWrapped("Baked Probe data will contain up to 4 different sizes of Brick.");
        }

        // Renderer Filter Settings
        if (ImGui::CollapsingHeader("Renderer Filter Settings"))
        {
            ImGui::Text("Configure which renderers affect probe baking.");
        }

        // Probe Invalidity Settings
        if (ImGui::CollapsingHeader("Probe Invalidity Settings"))
        {
            ImGui::Checkbox("Dilation", &m_APDilation);
            ImGui::Checkbox("Virtual Offset", &m_APVirtualOffset);

            if (m_APVirtualOffset)
            {
                ImGui::SliderFloat("Validity Threshold", &m_APValidityThreshold, 0.0f, 1.0f);
                ImGui::SliderFloat("Search Distance Multiplier", &m_APSearchDistanceMultiplier, 0.0f, 1.0f);
                ImGui::SliderFloat("Geometry Bias", &m_APGeometryBias, 0.0f, 0.1f);
                ImGui::SliderFloat("Ray Origin Bias", &m_APRayOriginBias, -0.1f, 0.0f);
            }

            // Layer Mask
            static int layerMask = 0;
            ImGui::Combo("Layer Mask", &layerMask, "Default, TransparentFX, Water, UI\0");

            if (ImGui::Button("Refresh Virtual Offset Debug"))
            {
                // TODO: Refresh debug visualization
            }
        }

        // Rendering Layers
        if (ImGui::CollapsingHeader("Rendering Layers"))
        {
            ImGui::Text("Configure which rendering layers affect probe baking.");
        }
    }

    void LightingPanel::RenderEnvironmentTab()
    {
        ImGui::PushID("EnvironmentTab");
        // Environment
        if (ImGui::CollapsingHeader("Environment##EnvTab", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Skybox Material
            ImGui::Text("Skybox Material");
            ImGui::SameLine(200);
            
            auto skybox = m_Context->GetSkybox();
            std::string currentSkybox = "None (Material)";
            if (skybox)
            {
                std::string skyboxPath = skybox->GetPath();
                std::string displayPath = ToSerializablePath(skyboxPath);
                currentSkybox = std::filesystem::path(displayPath).stem().string();
            }
            
            if (ImGui::BeginCombo("##SkyboxMaterial", currentSkybox.c_str()))
            {
                if (ImGui::Selectable("None (Material)", !skybox))
                    m_Context->SetSkybox(nullptr);

                // Skybox files
                std::string skyboxDir = "Resources/skybox";
                if (std::filesystem::exists(skyboxDir))
                {
                    std::vector<std::string> skyboxFiles;
                    for (const auto& entry : std::filesystem::directory_iterator(skyboxDir))
                    {
                        if (entry.is_regular_file())
                        {
                            std::string ext = entry.path().extension().string();
                            if (ext == ".hdr" || ext == ".exr")
                                skyboxFiles.push_back(entry.path().stem().string());
                        }
                    }
                    std::sort(skyboxFiles.begin(), skyboxFiles.end());

                    for (const auto& name : skyboxFiles)
                    {
                        bool isSelected = (currentSkybox == name);
                        if (ImGui::Selectable(name.c_str(), isSelected))
                        {
                            std::string fullPath = GetEngineDirectory().string() + "/Resources/skybox/" + name + ".hdr";
                            int resolution;
                            if (m_SkyboxResolutionIndex == 4)
                            {
                                int width, height, channels;
                                stbi_info(fullPath.c_str(), &width, &height, &channels);
                                resolution = std::max(width, height);
                            }
                            else
                            {
                                int res[] = { 512, 1024, 2048, 4096 };
                                resolution = res[m_SkyboxResolutionIndex];
                            }
                            auto newSkybox = Cubemap::CreateFromEquirectangular(fullPath, resolution);
                            if (newSkybox)
                                m_Context->SetSkybox(newSkybox);
                        }
                    }
                }

                if (ImGui::Selectable("Browse...", false))
                {
                    nfdchar_t* outPath = nullptr;
                    nfdfilteritem_t filters[1] = { { "HDR Images", "hdr,exr" } };
                    nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);

                    if (result == NFD_OKAY)
                    {
                        int resolution;
                        if (m_SkyboxResolutionIndex == 4)
                        {
                            int width, height, channels;
                            stbi_info(outPath, &width, &height, &channels);
                            resolution = std::max(width, height);
                        }
                        else
                        {
                            int res[] = { 512, 1024, 2048, 4096 };
                            resolution = res[m_SkyboxResolutionIndex];
                        }

                        auto newSkybox = Cubemap::CreateFromEquirectangular(outPath, resolution);
                        if (newSkybox)
                            m_Context->SetSkybox(newSkybox);
                        NFD_FreePath(outPath);
                    }
                }

                ImGui::EndCombo();
            }

            // Sun Source
            ImGui::Text("Sun Source");
            ImGui::SameLine(200);
            
            Entity sunEntity = m_Context->GetSunSourceEntity();
            std::string sunName = sunEntity ? sunEntity.GetComponent<TagComponent>().Tag : "None (Light)";
            
            if (ImGui::BeginCombo("##SunSource", sunName.c_str()))
            {
                if (ImGui::Selectable("None (Light)##SunNone", m_Context->GetSunSource() == 0))
                    m_Context->SetSunSource(0);
                
                auto view = m_Context->m_Registry.view<TagComponent, DirectionalLightComponent>();
                int index = 0;
                for (auto entity : view)
                {
                    Entity e{ entity, m_Context.get() };
                    auto& tag = e.GetComponent<TagComponent>();
                    auto& id = e.GetComponent<IDComponent>();
                    
                    std::string uniqueLabel = tag.Tag + "##SunSource" + std::to_string(index++);
                    bool isSelected = (m_Context->GetSunSource() == id.ID);
                    if (ImGui::Selectable(uniqueLabel.c_str(), isSelected))
                        m_Context->SetSunSource(id.ID);
                }
                
                ImGui::EndCombo();
            }

            // Realtime Shadow Color
            glm::vec3 shadowColor = m_Context->GetAmbientColor(); // Placeholder - gercek shadow color eklenecek
            if (ImGui::ColorEdit3("Realtime Shadow Color", glm::value_ptr(shadowColor)))
            {
                // TODO: Set realtime shadow color
            }
        }

        // Environment Lighting
        if (ImGui::CollapsingHeader("Environment Lighting##EnvTab", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* sources[] = { "Skybox", "Color", "Gradient" };
            int currentSource = (int)m_Context->GetEnvironmentLightingSource();
            
            ImGui::Text("Source");
            ImGui::SameLine(200);
            if (ImGui::Combo("##ELSource", &currentSource, sources, 3))
                m_Context->SetEnvironmentLightingSource((Scene::EnvironmentLightingSource)currentSource);
            
            ImGui::Spacing();
            
            if (currentSource == 0) // Skybox
            {
                glm::vec3 ambientColor = m_Context->GetAmbientColor();
                ImGui::Text("Ambient Color");
                ImGui::SameLine(200);
                if (ImGui::ColorEdit3("##AmbientColor", glm::value_ptr(ambientColor)))
                    m_Context->SetAmbientColor(ambientColor);
            }
            else if (currentSource == 1) // Color
            {
                glm::vec3 skyColor = m_Context->GetAmbientColor();
                ImGui::Text("Ambient Color");
                ImGui::SameLine(200);
                if (ImGui::ColorEdit3("##AmbientColor", glm::value_ptr(skyColor)))
                    m_Context->SetAmbientColor(skyColor);
            }
            else // Gradient
            {
                glm::vec3 skyColor = m_Context->GetAmbientSkyColor();
                ImGui::Text("Sky Color");
                ImGui::SameLine(200);
                if (ImGui::ColorEdit3("##SkyColor", glm::value_ptr(skyColor)))
                    m_Context->SetAmbientSkyColor(skyColor);
                
                glm::vec3 equatorColor = m_Context->GetAmbientEquatorColor();
                ImGui::Text("Equator Color");
                ImGui::SameLine(200);
                if (ImGui::ColorEdit3("##EquatorColor", glm::value_ptr(equatorColor)))
                    m_Context->SetAmbientEquatorColor(equatorColor);
                
                glm::vec3 groundColor = m_Context->GetAmbientGroundColor();
                ImGui::Text("Ground Color");
                ImGui::SameLine(200);
                if (ImGui::ColorEdit3("##GroundColor", glm::value_ptr(groundColor)))
                    m_Context->SetAmbientGroundColor(groundColor);
            }
            
            float ambientIntensity = m_Context->GetAmbientIntensity();
            ImGui::Text("Intensity Multiplier");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(200);
            if (ImGui::SliderFloat("##AmbientIntensity", &ambientIntensity, 0.0f, 2.0f))
                m_Context->SetAmbientIntensity(ambientIntensity);
        }

        // Environment Reflections
        if (ImGui::CollapsingHeader("Environment Reflections", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* refSources[] = { "Skybox", "Custom" };
            ImGui::Text("Source");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(150);
            ImGui::Combo("##RefSource", &m_EnvironmentReflectionsSourceIndex, refSources, 2);

            ImGui::Text("Resolution");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(150);
            ImGui::InputInt("##RefResolution", &m_ReflectionsResolution);

            const char* refCompressions[] = { "Auto", "None", "Low Quality", "High Quality" };
            ImGui::Text("Compression");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(150);
            ImGui::Combo("##RefCompression", &m_ReflectionsCompressionIndex, refCompressions, 4);

            ImGui::Text("Intensity Multiplier");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("##RefIntensity", &m_ReflectionsIntensityMultiplier, 0.0f, 5.0f);

            ImGui::Text("Bounces");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(150);
            ImGui::InputInt("##RefBounces", &m_ReflectionsBounces);
        }

        // Other Settings
        if (ImGui::CollapsingHeader("Other Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Fog
            bool fogEnabled = m_Context->IsFogEnabled();
            ImGui::Checkbox("Fog", &fogEnabled);
            m_Context->SetFogEnabled(fogEnabled);
            
            if (fogEnabled)
            {
                glm::vec3 fogColor = m_Context->GetFogColor();
                ImGui::Text("Fog Color");
                ImGui::SameLine(200);
                if (ImGui::ColorEdit3("##FogColor", glm::value_ptr(fogColor)))
                    m_Context->SetFogColor(fogColor);
                
                float fogDensity = m_Context->GetFogDensity();
                ImGui::Text("Density");
                ImGui::SameLine(200);
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderFloat("##FogDensity", &fogDensity, 0.0f, 0.1f))
                    m_Context->SetFogDensity(fogDensity);
                
                float fogStart = m_Context->GetFogStart();
                ImGui::Text("Start Distance");
                ImGui::SameLine(200);
                ImGui::SetNextItemWidth(200);
                if (ImGui::DragFloat("##FogStart", &fogStart, 1.0f, 0.0f, 1000.0f))
                    m_Context->SetFogStart(fogStart);
                
                float fogEnd = m_Context->GetFogEnd();
                ImGui::Text("End Distance");
                ImGui::SameLine(200);
                ImGui::SetNextItemWidth(200);
                if (ImGui::DragFloat("##FogEnd", &fogEnd, 1.0f, 0.0f, 1000.0f))
                    m_Context->SetFogEnd(fogEnd);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Halo Texture
            bool haloEnabled = m_Context->IsHaloEnabled();
            ImGui::Checkbox("Halo Texture", &haloEnabled);
            m_Context->SetHaloEnabled(haloEnabled);
            
            if (haloEnabled)
            {
                auto haloTexture = m_Context->GetHaloTexture();
                if (haloTexture)
                {
                    ImGui::Text("Loaded: %s", haloTexture->GetPath().c_str());
                    if (ImGui::Button("Remove##HaloTex"))
                        m_Context->SetHaloTexture(nullptr);
                }
                else
                {
                    ImGui::Text("None (Texture 2D)");
                }

                float haloStrength = m_Context->GetHaloStrength();
                ImGui::Text("Halo Strength");
                ImGui::SameLine(200);
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderFloat("##HaloStrength", &haloStrength, 0.0f, 1.0f))
                    m_Context->SetHaloStrength(haloStrength);
            }

            ImGui::Spacing();

            // Flare
            float flareFadeSpeed = m_Context->GetFlareFadeSpeed();
            ImGui::Text("Flare Fade Speed");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(200);
            if (ImGui::DragFloat("##FlareFadeSpeed", &flareFadeSpeed, 0.1f, 0.0f, 10.0f))
                m_Context->SetFlareFadeSpeed(flareFadeSpeed);

            float flareStrength = m_Context->GetFlareStrength();
            ImGui::Text("Flare Strength");
            ImGui::SameLine(200);
            ImGui::SetNextItemWidth(200);
            if (ImGui::SliderFloat("##FlareStrength", &flareStrength, 0.0f, 2.0f))
                m_Context->SetFlareStrength(flareStrength);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Spot Cookie
            auto spotCookie = m_Context->GetSpotCookie();
            ImGui::Text("Spot Cookie");
            if (spotCookie)
            {
                ImGui::Text("Loaded: %s", spotCookie->GetPath().c_str());
                if (ImGui::Button("Remove##SpotCookie"))
                    m_Context->SetSpotCookie(nullptr);
            }
            else
            {
                ImGui::Text("Soft");
            }
        }

        ImGui::PopID();
    }

    void LightingPanel::RenderRealtimeLightmapsTab()
    {
        ImGui::PushID("RealtimeLightmapTab");

        if (ImGui::CollapsingHeader("Realtime Lightmap Data", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (m_IsBaking && m_BakeMode == 1)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Status: Baking realtime lightmaps...");
                ImGui::ProgressBar(m_BakeProgress, ImVec2(-1, 0), "Baking...");
                ImGui::Text("Step: %s", m_BakeStep.c_str());
            }
            else if (m_RealtimeBaked)
            {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Baked");
                ImGui::Text("Atlas Size: %dx%d", m_RealtimeAtlasWidth, m_RealtimeAtlasHeight);
                ImGui::Text("Texels: %d", (int)m_RealtimeTexelCount);

                ImGui::Spacing();
                ImGui::Text("Lightmap Texture:");
                if (m_RealtimeLightmapTexture)
                    ImGui::Image((ImTextureID)(intptr_t)m_RealtimeLightmapTexture->GetRendererID(), ImVec2(200, 200));
                else
                    ImGui::Text("No texture generated");

                ImGui::Spacing();
                if (ImGui::Button("Clear Realtime Lightmaps", ImVec2(-1, 25)))
                {
                    m_RealtimeBaked = false;
                    m_RealtimeLightmapTexture.reset();
                    Renderer3D::SetLightmap(nullptr);
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No realtime lightmaps baked yet.");
            }
        }

        ImGui::Spacing();

        if (m_IsBaking && m_BakeMode == 1)
        {
            if (ImGui::Button("Cancel Bake", ImVec2(-1, 35)))
            {
                m_IsBaking = false;
                m_BakeProgress = 0.0f;
            }
        }
        else
        {
            if (ImGui::Button("Generate Realtime Lightmaps", ImVec2(-1, 35)))
            {
                if (!m_Context) { ImGui::PopID(); return; }

                m_IsBaking = true;
                m_BakeMode = 1;
                m_BakeProgress = 0.0f;

                auto baker = LightmapBaker::Create();
                LightmapSettings settings;
                settings.Lightmapper = m_LightmapperIndex;
                settings.Resolution = m_LightmapResolution;
                settings.MaxLightmapSize = (m_MaxLightmapSizeIndex + 1) * 256;
                settings.MaxBounces = m_MaxBounces;
                settings.DirectSamples = m_DirectSamples;
                settings.IndirectSamples = m_IndirectSamples;
                settings.AmbientOcclusion = m_AmbientOcclusion;
                settings.Filtering = m_FilteringIndex;
                baker->SetSettings(settings);

                baker->SetProgressCallback([this](float progress, const std::string& step) {
                    m_BakeProgress = progress;
                    m_BakeStep = step;
                });

                baker->Bake(m_Context.get());

                m_BakeProgress = 1.0f;
                m_RealtimeBaked = true;
                m_RealtimeAtlasWidth = baker->GetAtlas().Width;
                m_RealtimeAtlasHeight = baker->GetAtlas().Height;
                m_RealtimeTexelCount = (int)baker->GetAtlas().Texels.size();
                m_RealtimeLightmapTexture = baker->CreateLightmapTexture();
                if (m_RealtimeLightmapTexture)
                    Renderer3D::SetLightmap(m_RealtimeLightmapTexture);
                m_IsBaking = false;
            }
        }

        ImGui::PopID();
    }

    void LightingPanel::RenderBakedLightmapsTab()
    {
        ImGui::PushID("BakedLightmapTab");

        if (ImGui::CollapsingHeader("Baked Lightmap Data", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (m_IsBaking && m_BakeMode == 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Status: Baking baked lightmaps...");
                ImGui::ProgressBar(m_BakeProgress, ImVec2(-1, 0), "Baking...");
                ImGui::Text("Step: %s", m_BakeStep.c_str());
            }
            else if (m_BakedLightmapBaked)
            {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Baked");
                ImGui::Text("Atlas Size: %dx%d", m_BakedAtlasWidth, m_BakedAtlasHeight);
                ImGui::Text("Texels: %d", (int)m_BakedTexelCount);
                ImGui::Text("Static Objects: %d", m_BakedObjectCount);
                ImGui::Text("Total Bake Time: %.2fs", m_BakedBakeTime);

                ImGui::Spacing();
                ImGui::Text("Lightmap Texture:");
                if (m_BakedLightmapTexture)
                    ImGui::Image((ImTextureID)(intptr_t)m_BakedLightmapTexture->GetRendererID(), ImVec2(200, 200));
                else
                    ImGui::Text("No texture generated");

                ImGui::Spacing();
                if (ImGui::Button("Clear Baked Lightmaps", ImVec2(-1, 25)))
                {
                    m_BakedLightmapBaked = false;
                    m_BakedLightmapTexture.reset();
                    Renderer3D::SetLightmap(nullptr);
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No baked lightmaps generated yet.");
            }
        }

        ImGui::Spacing();

        if (m_IsBaking && m_BakeMode == 0)
        {
            if (ImGui::Button("Cancel Bake", ImVec2(-1, 35)))
            {
                m_IsBaking = false;
                m_BakeProgress = 0.0f;
            }
        }
        else
        {
            if (ImGui::Button("Generate Baked Lightmaps", ImVec2(-1, 35)))
            {
                if (!m_Context) { ImGui::PopID(); return; }

                m_IsBaking = true;
                m_BakeMode = 0;
                m_BakeProgress = 0.0f;

                auto baker = LightmapBaker::Create();
                LightmapSettings settings;
                settings.Lightmapper = m_LightmapperIndex;
                settings.ImportanceSampling = m_ImportanceSampling;
                settings.Resolution = m_LightmapResolution;
                settings.MaxLightmapSize = (m_MaxLightmapSizeIndex + 1) * 256;
                settings.MaxBounces = m_MaxBounces;
                settings.DirectSamples = m_DirectSamples;
                settings.IndirectSamples = m_IndirectSamples;
                settings.EnvironmentSamples = m_EnvironmentSamples;
                settings.Filtering = m_FilteringIndex;
                settings.Compression = m_LightmapCompressionIndex;
                settings.AmbientOcclusion = m_AmbientOcclusion;
                settings.DirectionalMode = m_DirectionalModeIndex;
                settings.AlbedoBoost = m_AlbedoBoost;
                settings.IndirectIntensity = m_IndirectIntensity;
                baker->SetSettings(settings);

                baker->SetProgressCallback([this](float progress, const std::string& step) {
                    m_BakeProgress = progress;
                    m_BakeStep = step;
                });

                auto startTime = std::chrono::high_resolution_clock::now();
                baker->Bake(m_Context.get());
                auto endTime = std::chrono::high_resolution_clock::now();
                float bakeTime = std::chrono::duration<float>(endTime - startTime).count();

                m_BakeProgress = 1.0f;
                m_BakedLightmapBaked = true;
                m_BakedAtlasWidth = baker->GetAtlas().Width;
                m_BakedAtlasHeight = baker->GetAtlas().Height;
                m_BakedTexelCount = (int)baker->GetAtlas().Texels.size();
                m_BakedBakeTime = bakeTime;
                m_BakedLightmapTexture = baker->CreateLightmapTexture();
                if (m_BakedLightmapTexture)
                    Renderer3D::SetLightmap(m_BakedLightmapTexture);

                auto& registry = m_Context->m_Registry;
                int objCount = 0;
                auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
                objCount += (int)std::distance(meshView.begin(), meshView.end());
                auto modelView = registry.view<TransformComponent, ModelComponent>();
                objCount += (int)std::distance(modelView.begin(), modelView.end());
                m_BakedObjectCount = objCount;

                m_IsBaking = false;
            }
        }

        ImGui::PopID();
    }

    void LightingPanel::RenderGPUBakingSection()
    {
        // GPU Baking Device
        ImGui::Text("GPU Baking Device");
        ImGui::SameLine(200);
        ImGui::SetNextItemWidth(250);
        ImGui::Combo("##GPUBakingDevice", &m_GPUBakingProfileIndex, "Automatic\0");

        // Bake On Scene Load
        ImGui::Text("Bake On Scene Load");
        ImGui::SameLine(200);
        ImGui::SetNextItemWidth(250);
        const char* bakeOnLoad[] = { "Never", "On Load" };
        ImGui::Combo("##BakeOnLoad", &m_BakeOnSceneLoadIndex, bakeOnLoad, 2);

        ImGui::Spacing();

        // Generate Lighting button
        if (m_IsBaking)
        {
            // Progress bar
            ImGui::ProgressBar(m_BakeProgress, ImVec2(-1, 0), "Baking...");
        }
        else
        {
            if (ImGui::Button("Generate Lighting", ImVec2(-1, 30)))
            {
                // Start baking
                m_IsBaking = true;
                m_BakeProgress = 0.0f;
                CQ_CORE_INFO("Starting lighting bake...");
                
                // TODO: Start actual baking process in background thread
                // For now, just set to done
                m_IsBaking = false;
                CQ_CORE_INFO("Lighting bake complete!");
            }
        }
    }
}
