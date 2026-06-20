#include "LightingPanel.h"
#include "Renderer/RHI/Cubemap.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

#include <imgui.h>
#include <nfd.h>
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <algorithm>

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

    static std::string ResolveSerializablePath(const std::string& path)
    {
        if (path.empty())
            return path;

        if (path.size() > 3 && path[0] == 'C' && path[1] == 'Q' && path[2] == ':')
        {
            std::string relative = path.substr(3);
            auto engineDir = GetEngineDirectory();
            auto absolute = engineDir / std::filesystem::path(relative);
            if (std::filesystem::exists(absolute))
                return absolute.string();
        }

        auto projectDir = Project::GetActiveProjectDirectory();
        if (!projectDir.empty())
        {
            std::filesystem::path p(path);
            if (p.is_relative())
            {
                auto absolute = projectDir / p;
                if (std::filesystem::exists(absolute))
                    return absolute.string();
            }
        }

        return path;
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

        // Environment bölümü
        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Skybox
            ImGui::Text("Skybox");
            ImGui::Separator();

            auto skybox = m_Context->GetSkybox();
            if (skybox)
            {
                ImGui::Text("Loaded: %dx%d", skybox->GetWidth(), skybox->GetHeight());
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Skybox Settings");
                
                // Exposure
                float exposure = m_Context->GetSkyboxExposure();
                if (ImGui::SliderFloat("Exposure", &exposure, 0.0f, 5.0f))
                {
                    m_Context->SetSkyboxExposure(exposure);
                }
                
                // Rotation
                float rotation = m_Context->GetSkyboxRotation();
                if (ImGui::SliderFloat("Rotation", &rotation, 0.0f, 360.0f))
                {
                    m_Context->SetSkyboxRotation(rotation);
                }
                
                // Tint
                glm::vec3 tint = m_Context->GetSkyboxTint();
                if (ImGui::ColorEdit3("Tint", glm::value_ptr(tint)))
                {
                    m_Context->SetSkyboxTint(tint);
                }
                
                ImGui::Spacing();
                ImGui::Separator();
            }

            // Resolution dropdown
            const char* resolutions[] = { "512x512", "1024x1024", "2048x2048", "4096x4096", "Original (HDR size)" };
            ImGui::Text("Cubemap Resolution:");
            ImGui::Combo("##SkyboxResolution", &m_SkyboxResolutionIndex, resolutions, 5);

            // Skybox dropdown (Resources/skybox klasöründeki HDR'ler)
            {
                std::vector<std::string> skyboxFiles;
                skyboxFiles.push_back("None");

                std::string skyboxDir = "Resources/skybox";
                if (std::filesystem::exists(skyboxDir))
                {
                    for (const auto& entry : std::filesystem::directory_iterator(skyboxDir))
                    {
                        if (entry.is_regular_file())
                        {
                            std::string ext = entry.path().extension().string();
                            if (ext == ".hdr" || ext == ".exr")
                                skyboxFiles.push_back(entry.path().stem().string());
                        }
                    }
                    std::sort(skyboxFiles.begin() + 1, skyboxFiles.end());
                }

                std::string currentSkybox = "None";
                if (skybox)
                {
                    std::string skyboxPath = skybox->GetPath();
                    std::string displayPath = ToSerializablePath(skyboxPath);
                    currentSkybox = std::filesystem::path(displayPath).stem().string();
                }

                if (ImGui::BeginCombo("Skybox", currentSkybox.c_str()))
                {
                    for (const auto& name : skyboxFiles)
                    {
                        bool isSelected = (currentSkybox == name);
                        if (ImGui::Selectable(name.c_str(), isSelected))
                        {
                            if (name == "None")
                            {
                                m_Context->SetSkybox(nullptr);
                            }
                            else
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

                    ImGui::Separator();

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
                            {
                                m_Context->SetSkybox(newSkybox);
                                CQ_CORE_INFO("Skybox loaded: {0} (resolution: {1}x{1})", outPath, resolution);
                            }
                            NFD_FreePath(outPath);
                        }
                    }

                    ImGui::EndCombo();
                }

                // Drag & Drop target (dropdown üzerine)
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        std::string droppedPath((const char*)payload->Data, payload->DataSize - 1);
                        std::filesystem::path path(droppedPath);
                        std::string ext = path.extension().string();

                        if (ext == ".hdr" || ext == ".exr")
                        {
                            int resolution;

                            if (m_SkyboxResolutionIndex == 4)
                            {
                                int width, height, channels;
                                stbi_info(droppedPath.c_str(), &width, &height, &channels);
                                resolution = std::max(width, height);
                            }
                            else
                            {
                                int res[] = { 512, 1024, 2048, 4096 };
                                resolution = res[m_SkyboxResolutionIndex];
                            }

                            auto newSkybox = Cubemap::CreateFromEquirectangular(droppedPath, resolution);
                            if (newSkybox)
                            {
                                m_Context->SetSkybox(newSkybox);
                                CQ_CORE_INFO("Skybox loaded: {0}", droppedPath);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            
            // Sun Source
            ImGui::Text("Sun Source");
            ImGui::Separator();
            
            Entity sunEntity = m_Context->GetSunSourceEntity();
            std::string sunName = sunEntity ? sunEntity.GetComponent<TagComponent>().Tag : "None (Light)";
            
            if (ImGui::BeginCombo("##SunSource", sunName.c_str()))
            {
                // "None" seçeneği
                if (ImGui::Selectable("None (Light)##SunNone", m_Context->GetSunSource() == 0))
                {
                    m_Context->SetSunSource(0);
                }
                
                // Directional Light'ları listele
                auto view = m_Context->m_Registry.view<TagComponent, DirectionalLightComponent>();
                int index = 0;
                for (auto entity : view)
                {
                    Entity e{ entity, m_Context.get() };
                    auto& tag = e.GetComponent<TagComponent>();
                    auto& id = e.GetComponent<IDComponent>();
                    
                    // Unique ID için tag + index kullan
                    std::string uniqueLabel = tag.Tag + "##SunSource" + std::to_string(index++);
                    
                    bool isSelected = (m_Context->GetSunSource() == id.ID);
                    if (ImGui::Selectable(uniqueLabel.c_str(), isSelected))
                    {
                        m_Context->SetSunSource(id.ID);
                    }
                }
                
                ImGui::EndCombo();
            }
        }

        // Ambient Light
        if (ImGui::CollapsingHeader("Ambient Light"))
        {
            // Source dropdown
            const char* sources[] = { "Skybox", "Color", "Gradient" };
            int currentSource = (int)m_Context->GetEnvironmentLightingSource();
            
            if (ImGui::Combo("Source", &currentSource, sources, 3))
            {
                m_Context->SetEnvironmentLightingSource((Scene::EnvironmentLightingSource)currentSource);
            }
            
            ImGui::Spacing();
            
            // Source'a göre renk seçenekleri
            if (currentSource == 0) // Skybox
            {
                glm::vec3 ambientColor = m_Context->GetAmbientColor();
                if (ImGui::ColorEdit3("Ambient Color", glm::value_ptr(ambientColor)))
                {
                    m_Context->SetAmbientColor(ambientColor);
                }
            }
            else if (currentSource == 1) // Color
            {
                glm::vec3 skyColor = m_Context->GetAmbientColor();
                if (ImGui::ColorEdit3("Sky Color", glm::value_ptr(skyColor)))
                {
                    m_Context->SetAmbientColor(skyColor);
                }
            }
            else // Gradient
            {
                glm::vec3 skyColor = m_Context->GetAmbientSkyColor();
                if (ImGui::ColorEdit3("Sky Color", glm::value_ptr(skyColor)))
                {
                    m_Context->SetAmbientSkyColor(skyColor);
                }
                
                glm::vec3 equatorColor = m_Context->GetAmbientEquatorColor();
                if (ImGui::ColorEdit3("Equator Color", glm::value_ptr(equatorColor)))
                {
                    m_Context->SetAmbientEquatorColor(equatorColor);
                }
                
                glm::vec3 groundColor = m_Context->GetAmbientGroundColor();
                if (ImGui::ColorEdit3("Ground Color", glm::value_ptr(groundColor)))
                {
                    m_Context->SetAmbientGroundColor(groundColor);
                }
            }
            
            float ambientIntensity = m_Context->GetAmbientIntensity();
            if (ImGui::SliderFloat("Intensity Multiplier", &ambientIntensity, 0.0f, 2.0f))
            {
                m_Context->SetAmbientIntensity(ambientIntensity);
            }
        }

        // Fog
        if (ImGui::CollapsingHeader("Fog"))
        {
            bool fogEnabled = m_Context->IsFogEnabled();
            if (ImGui::Checkbox("Enable Fog", &fogEnabled))
            {
                m_Context->SetFogEnabled(fogEnabled);
            }
            
            if (fogEnabled)
            {
                glm::vec3 fogColor = m_Context->GetFogColor();
                if (ImGui::ColorEdit3("Fog Color", glm::value_ptr(fogColor)))
                {
                    m_Context->SetFogColor(fogColor);
                }
                
                float fogDensity = m_Context->GetFogDensity();
                if (ImGui::SliderFloat("Density", &fogDensity, 0.0f, 0.1f))
                {
                    m_Context->SetFogDensity(fogDensity);
                }
                
                float fogStart = m_Context->GetFogStart();
                if (ImGui::DragFloat("Start Distance", &fogStart, 1.0f, 0.0f, 1000.0f))
                {
                    m_Context->SetFogStart(fogStart);
                }
                
                float fogEnd = m_Context->GetFogEnd();
                if (ImGui::DragFloat("End Distance", &fogEnd, 1.0f, 0.0f, 1000.0f))
                {
                    m_Context->SetFogEnd(fogEnd);
                }
            }
        }
        
        // Other Settings (Halo/Flare)
        if (ImGui::CollapsingHeader("Other Settings"))
        {
            // Halo
            bool haloEnabled = m_Context->IsHaloEnabled();
            if (ImGui::Checkbox("Enable Halo", &haloEnabled))
            {
                m_Context->SetHaloEnabled(haloEnabled);
            }
            
            if (haloEnabled)
            {
                float haloStrength = m_Context->GetHaloStrength();
                if (ImGui::SliderFloat("Halo Strength", &haloStrength, 0.0f, 1.0f))
                {
                    m_Context->SetHaloStrength(haloStrength);
                }
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            
            // Flare
            bool flareEnabled = m_Context->IsFlareEnabled();
            if (ImGui::Checkbox("Enable Flare", &flareEnabled))
            {
                m_Context->SetFlareEnabled(flareEnabled);
            }
            
            if (flareEnabled)
            {
                ImGui::Text("Flare Texture");
                
                // Drag & Drop target (Content Browser'dan texture sürükleme)
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        std::string droppedPath((const char*)payload->Data, payload->DataSize - 1);
                        std::filesystem::path path(droppedPath);
                        std::string ext = path.extension().string();
                        
                        CQ_CORE_INFO("Flare Texture: Drag & Drop received - Path: {0}, Extension: {1}", droppedPath, ext);
                        
                        // Sadece image dosyalarını kabul et
                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
                        {
                            auto texture = Texture2D::Create(droppedPath);
                            if (texture)
                            {
                                m_Context->SetHaloTexture(texture);
                                CQ_CORE_INFO("Flare texture loaded from drag & drop: {0}", droppedPath);
                            }
                            else
                            {
                                CQ_CORE_ERROR("Flare Texture: Failed to load {0}", droppedPath);
                            }
                        }
                        else
                        {
                            CQ_CORE_WARN("Flare Texture: Invalid file type {0}, only image files accepted", ext);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                
                auto haloTexture = m_Context->GetHaloTexture();
                if (haloTexture)
                {
                    ImGui::Text("Loaded: %s", haloTexture->GetPath().c_str());
                    if (ImGui::Button("Remove Flare Texture"))
                    {
                        m_Context->SetHaloTexture(nullptr);
                    }
                }
                else
                {
                    ImGui::Text("None (Texture 2D)");
                }
                
                if (ImGui::Button("Load Flare Texture..."))
                {
                    nfdchar_t* outPath = nullptr;
                    nfdfilteritem_t filters[1] = { { "Images", "jpg,jpeg,png,bmp,tga" } };
                    nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);
                    
                    if (result == NFD_OKAY)
                    {
                        auto texture = Texture2D::Create(outPath);
                        if (texture)
                        {
                            m_Context->SetHaloTexture(texture);
                            CQ_CORE_INFO("Flare texture loaded: {0}", outPath);
                        }
                        NFD_FreePath(outPath);
                    }
                }
                
                ImGui::Spacing();
                
                float flareFadeSpeed = m_Context->GetFlareFadeSpeed();
                if (ImGui::DragFloat("Flare Fade Speed", &flareFadeSpeed, 0.1f, 0.0f, 10.0f))
                {
                    m_Context->SetFlareFadeSpeed(flareFadeSpeed);
                }
                
                float flareStrength = m_Context->GetFlareStrength();
                if (ImGui::SliderFloat("Flare Strength", &flareStrength, 0.0f, 2.0f))
                {
                    m_Context->SetFlareStrength(flareStrength);
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Flare Elements");
                
                // Element count
                int elementCount = m_Context->GetFlareElementCount();
                if (ImGui::SliderInt("Element Count", &elementCount, 1, 10))
                {
                    m_Context->SetFlareElementCount(elementCount);
                }
                
                ImGui::Spacing();
                
                // Her element için ayarlar
                for (int i = 0; i < elementCount; i++)
                {
                    ImGui::PushID(i);
                    
                    if (ImGui::TreeNode(("Element " + std::to_string(i + 1)).c_str()))
                    {
                        auto element = m_Context->GetFlareElement(i);
                        bool changed = false;
                        
                        if (ImGui::ColorEdit3("Color Multiplier", glm::value_ptr(element.ColorMultiplier)))
                            changed = true;
                        
                        if (ImGui::SliderFloat("Size", &element.Size, 0.01f, 0.2f))
                            changed = true;
                        
                        if (ImGui::SliderFloat("Offset", &element.Offset, 0.0f, 1.0f))
                            changed = true;
                        
                        if (ImGui::SliderFloat("Intensity", &element.Intensity, 0.0f, 2.0f))
                            changed = true;
                        
                        if (changed)
                        {
                            m_Context->SetFlareElement(i, element);
                        }
                        
                        ImGui::TreePop();
                    }
                    
                    ImGui::PopID();
                }
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            
            ImGui::Spacing();
            ImGui::Separator();
            
            // Spot Cookie
            ImGui::Text("Spot Cookie");
            
            // Drag & Drop target (Content Browser'dan texture sürükleme)
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::string droppedPath((const char*)payload->Data, payload->DataSize - 1);
                    std::filesystem::path path(droppedPath);
                    std::string ext = path.extension().string();
                    
                    CQ_CORE_INFO("Spot Cookie: Drag & Drop received - Path: {0}, Extension: {1}", droppedPath, ext);
                    
                    // Sadece image dosyalarını kabul et
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
                    {
                        auto texture = Texture2D::Create(droppedPath);
                        if (texture)
                        {
                            m_Context->SetSpotCookie(texture);
                            CQ_CORE_INFO("Spot cookie loaded from drag & drop: {0}", droppedPath);
                        }
                        else
                        {
                            CQ_CORE_ERROR("Spot Cookie: Failed to load {0}", droppedPath);
                        }
                    }
                    else
                    {
                        CQ_CORE_WARN("Spot Cookie: Invalid file type {0}, only image files accepted", ext);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            
            auto spotCookie = m_Context->GetSpotCookie();
            if (spotCookie)
            {
                ImGui::Text("Loaded: %s", spotCookie->GetPath().c_str());
                if (ImGui::Button("Remove Spot Cookie"))
                {
                    m_Context->SetSpotCookie(nullptr);
                }
            }
            else
            {
                ImGui::Text("Soft");
            }
            
            if (ImGui::Button("Load Spot Cookie..."))
            {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filters[1] = { { "Images", "jpg,jpeg,png,bmp,tga" } };
                nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);
                
                if (result == NFD_OKAY)
                {
                    auto texture = Texture2D::Create(outPath);
                    if (texture)
                    {
                        m_Context->SetSpotCookie(texture);
                        CQ_CORE_INFO("Spot cookie loaded: {0}", outPath);
                    }
                    NFD_FreePath(outPath);
                }
            }
        }

        ImGui::End();
    }
}
