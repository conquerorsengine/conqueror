#include "CustomRenderTextureInspector.h"

#include "Assets/AssetUI.h"
#include "Panels/ObjectPicker.h"
#include "Renderer/CustomRenderTexture.h"
#include "Renderer/RenderTargetRegistry.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>

namespace Conqueror::Editor::CustomRenderTextureInspector
{
    void Draw(const std::string& path)
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_TV);
        ImGui::SameLine();
        ImGui::TextUnformatted(std::filesystem::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Custom Render Texture");
        ImGui::Separator();
        CustomRenderTextureAsset a;
        if (!CustomRenderTextures::Load(path, a))
        {
            UI::InfoBox("The custom render texture could not be read.", UI::InfoType::Error);
            return;
        }
        bool c = false;
        c |= UI::DragInt("Width", a.Width, 1.0f, 1, 4096);
        c |= UI::DragInt("Height", a.Height, 1.0f, 1, 4096);
        UI::BeginProperty("Shader", "GLSL file with  vec4 Update(vec2 uv). Create > Shader > Custom Render Texture Shader makes one.");
        c |= AssetUI::AssetSlot("shader", "Shader", { ".glsl", ".frag" }, a.Shader);
        UI::EndProperty();
        const char* modes[] = { "On Load", "Realtime", "On Demand" };
        c |= UI::Combo("Update Mode", a.UpdateMode, modes, 3, "Realtime: every frame (or every Update Period). On Demand: from scripts / the button below.");
        if (a.UpdateMode == 1)
            c |= UI::DragFloat("Update Period", a.UpdatePeriod, 0.01f, 0.0f, 60.0f, "%.2f s", "0 = every frame.");
        c |= UI::Checkbox("Double Buffered", a.DoubleBuffered, "The shader reads the previous frame in u_SelfTexture (simulations, trails).");
        c |= UI::Checkbox("Wrap (Repeat)", a.Repeat);
        UI::SubHeader("Initialization");
        c |= UI::Color4("Initialization Color", a.InitColor);
        UI::BeginProperty("Initialization Texture");
        c |= AssetUI::AssetSlot("init", "Texture", ObjectPicker::ImageExtensions(), a.InitTexture);
        UI::EndProperty();
        if (c)
            CustomRenderTextures::Save(path, a);

        auto tex = CustomRenderTextures::Acquire(path);
        ImGui::Spacing();
        if (ImGui::Button(ICON_FA_ROTATE "  Update Now"))
            CustomRenderTextures::RequestUpdate(path);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "  Initialize"))
            CustomRenderTextures::RequestUpdate(path, true);
        ImGui::SameLine();
        ImGui::TextDisabled("%llu updates", (unsigned long long)CustomRenderTextures::GetUpdateCount(path));
        const std::string error = CustomRenderTextures::GetError(path);
        if (!error.empty())
            UI::InfoBox(error.c_str(), UI::InfoType::Error);
        if (tex && tex->GetRendererID())
        {
            const float w = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
            const float h = w * (float)tex->GetHeight() / std::max(1.0f, (float)tex->GetWidth());
            ImGui::Image((ImTextureID)(uintptr_t)tex->GetRendererID(), ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Drag it into any texture slot (material Base Map, Sprite, UI Raw Image, Billboard...).");
    }
}
