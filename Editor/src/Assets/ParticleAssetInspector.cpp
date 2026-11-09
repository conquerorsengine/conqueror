#include "ParticleAssetInspector.h"

#include "Assets/AssetUI.h"
#include "Panels/InspectorPanel.h"
#include "Particles/ParticleSystem.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <filesystem>

namespace Conqueror::Editor::ParticleAssetInspector
{
    namespace fs = std::filesystem;

    bool IsParticleAsset(const std::string& path)
    {
        return fs::path(path).extension() == ParticleAssets::Extension;
    }

    void Draw(const std::string& path)
    {
        static std::string s_Path;
        static fs::file_time_type s_Time;
        static ParticleSystemComponent s_Proxy;
        static std::string s_Text;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Path != path || s_Time != time)
        {
            s_Path = path;
            s_Time = time;
            s_Proxy = ParticleSystemComponent{};
            ParticleAssets::Load(path, s_Proxy);
            s_Text = ParticleAssets::ToText(s_Proxy);
        }
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_FIRE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Particle System");
        ImGui::Separator();
        DrawParticleSystemUI(s_Proxy);
        const std::string now = ParticleAssets::ToText(s_Proxy);
        if (now != s_Text)
        {
            s_Text = now;
            ParticleAssets::Save(path, s_Proxy);
            s_Time = fs::last_write_time(path, ec);
        }
    }
}
