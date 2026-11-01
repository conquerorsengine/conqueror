#include "MaterialEditorUI.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "UI/EditorUI.h"
#include "ObjectPicker.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <nfd.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Conqueror::Editor::MaterialEditorUI
{
    namespace
    {
        struct Pending
        {
            std::weak_ptr<Material> Mat;
            std::string Path;
        };
        std::unordered_map<const Material*, Pending> s_Pending;
        std::function<void(const std::string&)> s_OpenGraph;

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        }

        bool IsImagePath(const std::string& path)
        {
            const std::string ext = Lower(fs::path(path).extension().string());
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr" || ext == ".cqrt";
        }

        // Unity doku yuvası: küçük önizleme + dosya adı (sürükle-bırak / dosya seç) + kaldır
        bool TextureSlot(const char* label, std::shared_ptr<Texture2D>& tex, const char* tooltip = nullptr)
        {
            bool changed = false;
            UI::BeginProperty(label, tooltip);
            const float h = ImGui::GetFrameHeight();
            if (tex && tex->GetRendererID())
                ImGui::Image((ImTextureID)(uintptr_t)tex->GetRendererID(), ImVec2(h, h), ImVec2(0, 1), ImVec2(1, 0));
            else
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + h, p.y + h), IM_COL32(110, 110, 120, 255));
                ImGui::Dummy(ImVec2(h, h));
            }
            ImGui::SameLine();
            std::string shown = tex && !tex->GetPath().empty() ? fs::path(tex->GetPath()).filename().string() : std::string("None (Texture)");
            const float clearW = (tex ? 2.0f : 1.0f) * (h + ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button((shown + "##slot").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - clearW, 0.0f)))
            {
                nfdu8char_t* outPath = nullptr;
                nfdfilteritem_t filters[1] = { { "Image Files", "png,jpg,jpeg,tga,bmp,hdr" } };
                std::string start = Project::GetActive() ? Project::GetAssetDirectory().string() : std::string();
                if (NFD_OpenDialogU8(&outPath, filters, 1, start.empty() ? nullptr : start.c_str()) == NFD_OKAY)
                {
                    tex = Texture2D::Create(outPath);
                    NFD_FreePathU8(outPath);
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered() && tex)
                ImGui::SetTooltip("%s", tex->GetPath().c_str());
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::string dropped((const char*)payload->Data, payload->DataSize - 1);
                    if (IsImagePath(dropped))
                    {
                        tex = Texture2D::Create(dropped);
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            const ImGuiID pickId = ImGui::GetID("##texpick");
            if (ImGui::Button(ICON_FA_BULLSEYE "##texpick", ImVec2(h, 0)))
                ObjectPicker::Open(pickId, "Texture", ObjectPicker::ImageExtensions(), tex ? tex->GetPath() : std::string());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Select Texture");
            if (std::string picked; ObjectPicker::Consume(pickId, picked))
            {
                tex = picked.empty() ? nullptr : Texture2D::Create(picked);
                changed = true;
            }
            if (tex)
            {
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_XMARK "##clear", ImVec2(h, 0)))
                {
                    tex = nullptr;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove texture");
            }
            UI::EndProperty();
            return changed;
        }

    }

    bool DrawProperties(Material& m, const Options& options)
    {
        bool changed = false;
        ImGui::PushID(&m);

        if (UI::BeginSection("Surface Options", true))
        {
            const char* shadingNames[] = { "Lit", "Subsurface", "Hair", "Cloth", "Unlit" };
            int shading = (int)m.Shading;
            if (options.Embedded)
            {
                bool unlit = m.Shading == Material::ShadingModel::Unlit;
                if (UI::Checkbox("Unlit", unlit, "No lighting: color = texture x color + emission (screens, LED walls)"))
                {
                    m.Shading = unlit ? Material::ShadingModel::Unlit : Material::ShadingModel::DefaultLit;
                    changed = true;
                }
            }
            else if (UI::Combo("Shader", shading, shadingNames, 5, "Lighting model (Unreal Shading Model / Unity Lit, Unlit)"))
            {
                m.Shading = (Material::ShadingModel)shading;
                changed = true;
            }
            const char* surfaceNames[] = { "Opaque", "Cutout", "Transparent" };
            int surface = (int)m.Rendering;
            if (UI::Combo("Surface Type", surface, surfaceNames, 3,
                          "Opaque; Cutout = alpha clipping (leaves, fences); Transparent = blended with what is behind"))
            {
                m.Rendering = (Material::RenderMode)surface;
                if (m.Rendering == Material::RenderMode::Transparent && m.Opacity >= 0.999f && options.Embedded)
                    m.Opacity = 0.5f;   // gömülü malzemede saydamlık renk alfasından gelir
                changed = true;
            }
            if (!options.Embedded)
            {
                const char* faceNames[] = { "Front", "Back", "Both" };
                int face = (int)m.Culling;
                if (UI::Combo("Render Face", face, faceNames, 3, "Which sides of the faces are drawn"))
                {
                    m.Culling = (Material::CullMode)face;
                    changed = true;
                }
            }
            if (m.Rendering == Material::RenderMode::Cutout)
                changed |= UI::SliderFloat("Alpha Clip Threshold", m.AlphaClip, 0.0f, 1.0f, "%.2f");
            UI::EndSection();
        }

        if (UI::BeginSection("Surface Inputs", true))
        {
            changed |= TextureSlot("Base Map", m.AlbedoMap, "Albedo / base color texture (multiplied by the color)");
            glm::vec4 color(m.Albedo, m.Opacity);
            if (UI::Color4("Base Color", color, "Alpha = opacity (Transparent surface)"))
            {
                m.Albedo = glm::vec3(color);
                m.Opacity = color.a;
                if (options.Embedded && m.Opacity < 0.999f && m.Rendering == Material::RenderMode::Opaque)
                    m.Rendering = Material::RenderMode::Transparent;
                changed = true;
            }

            if (options.Embedded)
            {
                // Gömülü malzeme: glTF ORM maske haritası (R = AO, G = pürüzlülük, B = metalik)
                std::shared_ptr<Texture2D> mask = m.MapsScaleFactors ? m.AOMap : nullptr;
                if (TextureSlot("Mask Map", mask, "glTF packed texture: R = ambient occlusion, G = roughness, B = metallic"))
                {
                    m.AOMap = m.RoughnessMap = m.MetallicMap = mask;
                    m.MapsScaleFactors = mask != nullptr;
                    m.RoughnessChannel = mask ? 1 : 0;
                    m.MetallicChannel = mask ? 2 : 0;
                    changed = true;
                }
            }
            else
            {
                changed |= TextureSlot("Metallic Map", m.MetallicMap);
            }
            changed |= UI::SliderFloat("Metallic", m.Metallic, 0.0f, 1.0f);
            if (!options.Embedded)
                changed |= TextureSlot("Roughness Map", m.RoughnessMap);
            changed |= UI::SliderFloat("Roughness", m.Roughness, 0.0f, 1.0f, "%.2f", "0 = mirror, 1 = matte (Unity Smoothness = 1 - Roughness)");
            if (!options.Embedded)
            {
                const char* channelNames[] = { "R", "G", "B", "A" };
                if (m.MetallicMap || m.RoughnessMap || m.AOMap)
                {
                    changed |= UI::Checkbox("Packed Maps", m.MapsScaleFactors,
                                            "Maps multiply the sliders and read single channels (glTF ORM: R = AO, G = roughness, B = metallic)");
                    if (m.MapsScaleFactors)
                    {
                        changed |= UI::Combo("Metallic Channel", m.MetallicChannel, channelNames, 4);
                        changed |= UI::Combo("Roughness Channel", m.RoughnessChannel, channelNames, 4);
                    }
                }
            }
            changed |= TextureSlot("Normal Map", m.NormalMap, "Tangent-space normal map (OpenGL convention, +Y up)");
            if (!options.Embedded)
                changed |= TextureSlot("Occlusion Map", m.AOMap);
            changed |= UI::SliderFloat("Occlusion", m.AO, 0.0f, 1.0f);

            if (m.Shading == Material::ShadingModel::Subsurface || m.Shading == Material::ShadingModel::Hair ||
                m.Shading == Material::ShadingModel::Cloth)
            {
                const char* label = m.Shading == Material::ShadingModel::Subsurface ? "Subsurface Color"
                                  : m.Shading == Material::ShadingModel::Hair ? "Secondary Highlight" : "Sheen Color";
                changed |= UI::Color3(label, m.SubsurfaceColor);
                changed |= UI::SliderFloat("Amount", m.SubsurfaceAmount, 0.0f, 1.0f);
            }
            UI::EndSection();
        }

        if (UI::BeginSection("Emission", m.EmissionStrength > 0.0f))
        {
            if (options.Embedded)
            {
                bool fromBase = m.EmissionMap != nullptr;
                ImGui::BeginDisabled(!m.AlbedoMap);
                if (UI::Checkbox("Use Base Map", fromBase, "Emission is multiplied by the base map (only windows glow, etc.)"))
                {
                    m.EmissionMap = fromBase ? m.AlbedoMap : nullptr;
                    changed = true;
                }
                ImGui::EndDisabled();
            }
            else
                changed |= TextureSlot("Emission Map", m.EmissionMap, "Emission is multiplied by this texture (only windows glow, etc.)");
            changed |= UI::Color3("Emission Color", m.EmissionColor);
            changed |= UI::DragFloat("Intensity", m.EmissionStrength, 0.05f, 0.0f, 100.0f, "%.2f", "HDR: values above 1 bloom");
            UI::EndSection();
        }

        if (UI::BeginSection("Tiling & Offset", true))
        {
            changed |= UI::Vec2("Tiling", m.Tiling, 0.05f);
            changed |= UI::Vec2("Offset", m.Offset, 0.01f);
            changed |= UI::Checkbox("World Aligned", m.WorldAligned,
                                    "UVs from the world position (three-axis projection): Tiling = repeats per meter. "
                                    "Scaled blocks keep the same texel size (Unreal WorldAlignedTexture)");
            if (m.WorldAligned)
                changed |= UI::Checkbox("Align To Object", m.AlignToObject, "Projection in object space: the texture moves with the object");
            UI::EndSection();
        }

        ImGui::PopID();
        return changed;
    }

    void MarkDirty(const std::shared_ptr<Material>& material, const std::string& path)
    {
        if (material && !path.empty())
            s_Pending[material.get()] = { material, path };
    }

    void FlushPendingSaves(bool force)
    {
        if (s_Pending.empty())
            return;
        if (!force && (ImGui::GetCurrentContext() && (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left))))
            return;
        for (auto& [ptr, pending] : s_Pending)
            if (auto mat = pending.Mat.lock())
                MaterialAsset::Save(mat, pending.Path);
        s_Pending.clear();
    }

    std::string UniqueMaterialPath(const std::string& directory, const std::string& name)
    {
        std::string clean;
        for (char c : name)
            clean += (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ') ? c : '_';
        if (clean.empty())
            clean = "New Material";
        fs::path p = fs::path(directory) / (clean + MaterialAsset::Extension);
        std::error_code ec;
        for (int i = 1; fs::exists(p, ec); ++i)
            p = fs::path(directory) / (clean + " " + std::to_string(i) + MaterialAsset::Extension);
        return p.string();
    }

    bool AssignMaterial(MeshRendererComponent& component, const std::string& path)
    {
        auto mat = MaterialAsset::Load(path);
        if (!mat)
            return false;
        component.MaterialInstance = mat;
        component.MaterialPath = path;
        component.RuntimeMaterial.reset();
        return true;
    }

    bool AssetPathSlot(const char* label, std::string& path, const char* extension, const char* typeName, const char* tooltip)
    {
        bool changed = false;
        UI::BeginProperty(label, tooltip);
        const float h = ImGui::GetFrameHeight();
        std::string shown = path.empty() ? std::string("None (") + typeName + ")" : fs::path(path).stem().string();
        ImGui::PushStyleColor(ImGuiCol_Text, path.empty() ? UI::Colors::TextDim : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        const float extra = (path.empty() ? 1 : 2) * (h + ImGui::GetStyle().ItemSpacing.x);
        ImGui::Button((shown + "##assetslot").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - extra, 0.0f));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !path.empty())
            ImGui::SetTooltip("%s", path.c_str());
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::string dropped((const char*)payload->Data, payload->DataSize - 1);
                if (Lower(fs::path(dropped).extension().string()) == extension)
                {
                    path = dropped;
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        const ImGuiID pickId = ImGui::GetID("##assetpick");
        if (ImGui::Button(ICON_FA_BULLSEYE "##assetpick", ImVec2(h, 0)))
            ObjectPicker::Open(pickId, typeName, { extension }, path);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select %s", typeName);
        if (std::string picked; ObjectPicker::Consume(pickId, picked))
        {
            path = picked;
            changed = true;
        }
        if (!path.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK "##assetclear", ImVec2(h, 0)))
            {
                path.clear();
                changed = true;
            }
        }
        UI::EndProperty();
        return changed;
    }

    void SetOpenGraphCallback(std::function<void(const std::string&)> callback) { s_OpenGraph = std::move(callback); }

    void OpenGraph(const std::string& materialPath)
    {
        if (s_OpenGraph)
            s_OpenGraph(materialPath);
    }

    // Gömülü malzeme düzenlendi: Mesh Renderer alanlarına geri yaz
    static void WriteBackEmbedded(MeshRendererComponent& c, const Material& m)
    {
        c.Color = glm::vec4(m.Albedo, m.Opacity);
        if (m.Rendering != Material::RenderMode::Transparent && c.Color.a < 0.999f)
            c.Color.a = 1.0f;
        c.Metallic = m.Metallic;
        c.Roughness = m.Roughness;
        c.AO = m.AO;
        c.EmissionColor = m.EmissionColor;
        c.EmissionStrength = m.EmissionStrength;
        c.Tiling = m.Tiling;
        c.Offset = m.Offset;
        c.WorldAlignedUV = m.WorldAligned;
        c.AlignToObject = m.AlignToObject;
        auto setTex = [](std::shared_ptr<Texture2D>& dst, std::string& path, const std::shared_ptr<Texture2D>& src) {
            dst = src;
            path = src ? src->GetPath() : std::string();
        };
        setTex(c.Texture, c.TexturePath, m.AlbedoMap);
        setTex(c.NormalMap, c.NormalMapPath, m.NormalMap);
        setTex(c.MaskMap, c.MaskMapPath, m.MapsScaleFactors ? m.AOMap : nullptr);
        c.EmissionFromTexture = m.EmissionMap != nullptr;   // gömülü malzemede ışıma haritası = taban dokusu
        c.Unlit = m.Shading == Material::ShadingModel::Unlit;
        c.AlphaClip = m.Rendering == Material::RenderMode::Cutout;
        c.AlphaClipThreshold = m.AlphaClip;
    }

    void DrawMeshRendererMaterials(MeshRendererComponent& component, const std::string& entityName)
    {

        if (component.MaterialInstance && !component.MaterialPath.empty())
            if (std::string now = MaterialAsset::PathOf(component.MaterialInstance.get()); !now.empty())
                component.MaterialPath = now;   // dosya taşındı / yeniden adlandırıldı

        // ---- Materials yuvası (Unity: Materials > Element 0)
        UI::BeginProperty("Material", "The material asset (.cqmat) this renderer draws with. Drag one from the Content Browser.");
        const float h = ImGui::GetFrameHeight();
        std::string shown = component.MaterialInstance
            ? (component.MaterialPath.empty() ? component.MaterialInstance->Name + " (Scene)"
                                              : fs::path(component.MaterialPath).stem().string() + (MaterialAsset::IsReadOnly(component.MaterialPath) ? " (Packages)" : ""))
            : std::string("Scene Material");
        ImGui::PushStyleColor(ImGuiCol_Text, component.MaterialPath.empty() ? UI::Colors::TextDim : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::Button((std::string(ICON_FA_CIRCLE_HALF_STROKE "  ") + shown + "##matslot").c_str(),
                      ImVec2(ImGui::GetContentRegionAvail().x - h - ImGui::GetStyle().ItemSpacing.x, 0.0f));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !component.MaterialPath.empty())
            ImGui::SetTooltip("%s", component.MaterialPath.c_str());
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::string dropped((const char*)payload->Data, payload->DataSize - 1);
                if (MaterialAsset::IsMaterialFile(dropped))
                    AssignMaterial(component, dropped);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        const ImGuiID matPickId = ImGui::GetID("##matpick");
        if (ImGui::Button(ICON_FA_BULLSEYE "##matpick", ImVec2(h, 0)))
            ObjectPicker::Open(matPickId, "Material", { ".cqmat" }, component.MaterialPath);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select Material");
        if (std::string picked; ObjectPicker::Consume(matPickId, picked))
        {
            if (picked.empty())
            {
                component.MaterialInstance = nullptr;
                component.MaterialPath.clear();
            }
            else
                AssignMaterial(component, picked);
        }
        UI::EndProperty();

        // ---- Oluştur / kaldır
        UI::BeginProperty("##matactions");
        if (ImGui::Button(ICON_FA_PLUS " New"))
        {
            // Unity "Create > Material" ile aynı: gerçek dosya. Mevcut görünüm (gömülü alanlar veya sahne malzemesi) kopyalanır.
            std::string dir = Project::GetActive() ? (Project::GetAssetDirectory() / "Materials").string() : std::string("Materials");
            std::string path = UniqueMaterialPath(dir, entityName.empty() ? "New Material" : entityName);
            Material from;
            if (component.MaterialInstance)
                from = *component.MaterialInstance;
            else
                FillMaterialFromMeshRenderer(from, component);
            if (auto mat = MaterialAsset::Create(path, &from))
            {
                component.MaterialInstance = mat;
                component.MaterialPath = path;
                CQ_CORE_INFO("Material created: {0}", path);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Save the current look as a new material asset in Assets/Materials and use it");
        if (component.MaterialInstance)
        {
            ImGui::SameLine();
            const bool packaged = MaterialAsset::IsReadOnly(component.MaterialPath);
            ImGui::BeginDisabled(packaged);
            const bool openGraph = ImGui::Button(ICON_FA_DIAGRAM_PROJECT " Graph");
            ImGui::EndDisabled();
            if (packaged && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Built-in material: click New to make an editable copy first");
            if (openGraph)
            {
                if (component.MaterialPath.empty())
                    OpenGraph(std::string());
                else
                    OpenGraph(component.MaterialPath);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK "##matremove"))
            {
                component.MaterialInstance = nullptr;
                component.MaterialPath.clear();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stop using the material (the asset file is kept)");
        }
        UI::EndProperty();

        // ---- Malzeme düzenleyici (Unity: Inspector'ın altında malzeme bölümü)
        ImGui::Spacing();
        if (component.MaterialInstance)
        {
            auto& mat = *component.MaterialInstance;
            ImGui::TextColored(UI::Colors::Accent, ICON_FA_CIRCLE_HALF_STROKE);
            ImGui::SameLine();
            ImGui::TextUnformatted(component.MaterialPath.empty() ? (mat.Name + " (stored in the scene)").c_str()
                                                                  : fs::path(component.MaterialPath).filename().string().c_str());
            if (!mat.GraphNodes.empty() && mat.GraphInitialized && mat.MaterialShader)
                UI::InfoBox("Shader Graph material: inputs wired in the graph override these values.", UI::InfoType::Info);
            if (component.MaterialPath.empty())
                UI::InfoBox("This material lives inside the scene. Use New Material to save it as an asset you can share.",
                            UI::InfoType::Warning);
            if (MaterialAsset::IsReadOnly(component.MaterialPath))
            {
                // Paket malzemesi (Unity Default-Material gibi): düzenlenemez, "New" ile kopyası çıkarılır
                UI::InfoBox("Built-in material from Packages (read-only). Click New to make an editable copy in your project.",
                            UI::InfoType::Info);
                ImGui::BeginDisabled();
                DrawProperties(mat);
                ImGui::EndDisabled();
            }
            else if (DrawProperties(mat))
                MarkDirty(component.MaterialInstance, component.MaterialPath);
        }
        else
        {
            ImGui::TextColored(UI::Colors::TextDim, ICON_FA_CIRCLE_HALF_STROKE "  Scene Material");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("This object's own material, stored in the scene. Click New to save it as a material asset.");
            Material temp;
            FillMaterialFromMeshRenderer(temp, component);
            Options opt;
            opt.Embedded = true;
            if (DrawProperties(temp, opt))
                WriteBackEmbedded(component, temp);
        }
    }

    void DrawAssetInspector(const std::string& path)
    {
        auto mat = MaterialAsset::Load(path);
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_CIRCLE_HALF_STROKE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Material");
        ImGui::Separator();
        if (!mat)
        {
            UI::InfoBox("The material file could not be read.", UI::InfoType::Error);
            return;
        }
        if (!mat->ParentPath.empty())
        {
            // Varyant: üst malzeme (Unity Material Variant "Parent")
            std::string parent = mat->ParentPath;
            if (AssetPathSlot("Parent", parent, ".cqmat", "Material", "Everything not changed here comes from this material."))
            {
                if (!parent.empty() && MaterialAsset::Load(parent).get() != mat.get())
                {
                    // Farklar korunur, yalnız üst değişir: dosyadaki "Parent" satırı yenilenip yeniden okunur
                    FlushPendingSaves(true);
                    std::ifstream in(path);
                    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), out;
                    in.close();
                    std::string rel = parent;
                    if (Project::GetActive())
                    {
                        std::error_code ec;
                        auto r = fs::relative(parent, Project::GetActiveProjectDirectory(), ec);
                        if (!ec && !r.empty() && r.generic_string().rfind("..", 0) != 0)
                            rel = r.generic_string();
                    }
                    std::istringstream lines(text);
                    for (std::string line; std::getline(lines, line);)
                        out += (line.rfind("  Parent:", 0) == 0 ? "  Parent: " + rel : line) + "\n";
                    std::ofstream(path, std::ios::trunc) << out;
                    MaterialAsset::CheckFileChanges();
                }
            }
            ImGui::TextDisabled("Variant: stores only the values you change.");
        }
        ImGui::BeginDisabled(MaterialAsset::IsReadOnly(path));
        if (ImGui::Button(ICON_FA_DIAGRAM_PROJECT " Open Shader Graph"))
            OpenGraph(path);
        ImGui::EndDisabled();
        if (!mat->GraphNodes.empty() && mat->GraphInitialized && mat->MaterialShader)
            UI::InfoBox("Shader Graph material: inputs wired in the graph override these values.", UI::InfoType::Info);
        if (MaterialAsset::IsReadOnly(path))
        {
            UI::InfoBox("Built-in material from Packages (read-only).", UI::InfoType::Info);
            ImGui::BeginDisabled();
            DrawProperties(*mat);
            ImGui::EndDisabled();
        }
        else if (DrawProperties(*mat))
            MarkDirty(mat, path);
    }
}
