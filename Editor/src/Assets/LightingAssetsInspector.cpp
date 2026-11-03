#include "LightingAssetsInspector.h"

#include "Assets/AssetUI.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Lightmap/LightmapParameters.h"
#include "Renderer/RenderSettings.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>

namespace Conqueror::Editor::LightingAssetsInspector
{
    namespace fs = std::filesystem;

    static void Header(const char* icon, const std::string& path, const char* kind)
    {
        ImGui::TextColored(UI::Colors::Accent, "%s", icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kind);
        ImGui::Separator();
    }

    void DrawLightingSettings(const std::string& path)
    {
        Header(ICON_FA_LIGHTBULB, path, "Lighting Settings");
        LightBakeSettings b;
        if (!SceneSerializer::LoadLightingSettings(path, b))
        {
            UI::InfoBox("The lighting settings could not be read.", UI::InfoType::Error);
            return;
        }
        bool c = false;
        c |= UI::Checkbox("Realtime Global Illumination", b.RealtimeGI);
        c |= UI::Checkbox("Baked Global Illumination", b.BakedGI);
        const char* modes[] = { "Shadowmask", "Distance Shadowmask", "Subtractive", "Baked Indirect" };
        c |= UI::Combo("Lighting Mode", b.LightingMode, modes, 4);
        const char* lightmappers[] = { "None", "Progressive CPU", "Progressive GPU (preview)" };
        c |= UI::Combo("Lightmapper", b.Lightmapper, lightmappers, 3);
        c |= UI::DragInt("Direct Samples", b.DirectSamples, 1.0f, 1, 4096);
        c |= UI::DragInt("Indirect Samples", b.IndirectSamples, 4.0f, 8, 16384);
        c |= UI::DragInt("Environment Samples", b.EnvironmentSamples, 4.0f, 8, 8192);
        c |= UI::SliderInt("Max Bounces", b.MaxBounces, 0, 8);
        c |= UI::DragInt("Lightmap Resolution", b.Resolution, 0.25f, 1, 400, "Texels per unit");
        const int sizes[] = { 256, 512, 1024, 2048, 4096 };
        const char* sizeNames[] = { "256", "512", "1024", "2048", "4096" };
        int sizeIndex = 2;
        for (int i = 0; i < 5; ++i) if (sizes[i] == b.MaxLightmapSize) sizeIndex = i;
        if (UI::Combo("Max Lightmap Size", sizeIndex, sizeNames, 5)) { b.MaxLightmapSize = sizes[sizeIndex]; c = true; }
        c |= UI::Checkbox("Ambient Occlusion", b.AmbientOcclusion);
        c |= UI::SliderFloat("Albedo Boost", b.AlbedoBoost, 1.0f, 10.0f);
        c |= UI::SliderFloat("Indirect Intensity", b.IndirectIntensity, 0.0f, 5.0f);
        UI::BeginProperty("Default Lightmap Parameters");
        c |= AssetUI::AssetSlot("lmp", "Lightmap Parameters", { LightmapParameterAssets::Extension }, b.DefaultLightmapParameters);
        UI::EndProperty();
        if (c)
            SceneSerializer::SaveLightingSettings(path, b);
        ImGui::Spacing();
        ImGui::TextWrapped("Assign it in Window > Lighting > Scene (Lighting Settings Asset). Every scene that uses it bakes with these settings.");
    }

    void DrawLightmapParameters(const std::string& path)
    {
        Header(ICON_FA_SLIDERS, path, "Lightmap Parameters");
        LightmapParameters p;
        LightmapParameterAssets::Load(path, p);
        bool c = false;
        c |= UI::SliderInt("Anti-aliasing Samples", p.AntiAliasingSamples, 1, 16, "Sub-samples per lightmap texel (smoother edges, slower bake).");
        c |= UI::SliderFloat("Backface Tolerance", p.BackfaceTolerance, 0.0f, 1.0f, "%.2f",
                             "Share of rays that must hit front faces; texels inside geometry are filled from their neighbours.");
        c |= UI::DragFloat("Pushoff", p.Pushoff, 0.001f, 0.0f, 1.0f, "%.3f m");
        c |= UI::SliderFloat("Indirect Sample Scale", p.IndirectSampleScale, 0.1f, 8.0f, "%.2f");
        if (c)
            LightmapParameterAssets::Save(path, p);
        ImGui::Spacing();
        ImGui::TextWrapped("Use it as the scene default (Lighting Settings) or on a Mesh Renderer (Lighting > Lightmap Parameters).");
    }
}
