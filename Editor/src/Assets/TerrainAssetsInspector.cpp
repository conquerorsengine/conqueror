#include "TerrainAssetsInspector.h"

#include "Assets/AssetUI.h"
#include "Panels/ObjectPicker.h"
#include "Renderer/RHI/Texture.h"
#include "Terrain/Terrain.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>
#include <map>

namespace Conqueror::Editor::TerrainAssetsInspector
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

    void DrawLayer(const std::string& path)
    {
        Header(ICON_FA_LAYER_GROUP, path, "Terrain Layer");
        TerrainLayer l;
        TerrainAssets::LoadLayer(path, l);
        bool c = false;
        UI::BeginProperty("Diffuse");
        std::string tex = l.TexturePath.empty() ? std::string() : AssetUI::ToProjectRelative(l.TexturePath);
        if (AssetUI::AssetSlot("diffuse", "Texture", ObjectPicker::ImageExtensions(), tex))
        {
            l.TexturePath = tex.empty() ? std::string() : AssetUI::Resolve(tex).string();
            c = true;
        }
        UI::EndProperty();
        c |= UI::Color3("Tint", l.Tint);
        c |= UI::DragFloat("Tiling", l.Tiling, 0.1f, 0.1f, 1000.0f, "%.1f", "Repeats across the terrain.");
        c |= UI::SliderFloat("Roughness", l.Roughness, 0.0f, 1.0f);
        c |= UI::SliderFloat("Metallic", l.Metallic, 0.0f, 1.0f);
        if (c)
            TerrainAssets::SaveLayer(path, l);
        static auto* thumbs = new std::map<std::string, std::shared_ptr<Texture2D>>();
        if (!l.TexturePath.empty())
        {
            auto& t = (*thumbs)[l.TexturePath];
            if (!t) t = Texture2D::Create(l.TexturePath);
            if (t && t->GetRendererID())
                ImGui::ImageWithBg((ImTextureID)(uintptr_t)t->GetRendererID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0),
                                   ImVec4(0, 0, 0, 0), ImVec4(l.Tint.r, l.Tint.g, l.Tint.b, 1.0f));
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Pick it in Terrain > Texture Layers (Terrain Layer). Every terrain that uses it updates when you change it.");
    }

    void DrawBrush(const std::string& path)
    {
        Header(ICON_FA_PAINTBRUSH, path, "Brush");
        TerrainBrushAsset b;
        TerrainAssets::LoadBrush(path, b);
        bool c = false;
        UI::BeginProperty("Mask Texture", "Grayscale image: white = full effect, black = none.");
        c |= AssetUI::AssetSlot("mask", "Texture", ObjectPicker::ImageExtensions(), b.Mask);
        UI::EndProperty();
        c |= UI::SliderFloat("Falloff", b.Falloff, 0.0f, 1.0f, "%.2f", "Extra softening towards the brush edge.");
        c |= UI::SliderFloat("Rotation", b.Rotation, -180.0f, 180.0f, "%.0f deg");
        c |= UI::SliderFloat("Scale", b.Scale, 0.05f, 4.0f, "%.2f");
        c |= UI::Checkbox("Invert", b.Invert);
        if (c)
            TerrainAssets::SaveBrush(path, b);
        // Önizleme: maskenin etkisi (döndürme, ters, yumuşatma dahil) 64x64
        if (!b.Mask.empty())
        {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float size = 160.0f, cell = size / 48.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rot = b.Rotation * 3.14159265f / 180.0f, cr = std::cos(rot), sr = std::sin(rot);
            for (int y = 0; y < 48; ++y)
                for (int x = 0; x < 48; ++x)
                {
                    const float dx = (x + 0.5f) / 48.0f - 0.5f, dy = 0.5f - (y + 0.5f) / 48.0f;
                    const float qx = dx * cr + dy * sr, qy = -dx * sr + dy * cr;
                    float m = 0.0f;
                    if (std::abs(qx) <= 0.5f && std::abs(qy) <= 0.5f && TerrainAssets::SampleBrushMask(b.Mask, qx + 0.5f, qy + 0.5f, m))
                    {
                        if (b.Invert) m = 1.0f - m;
                        if (b.Falloff > 0.0f)
                            m *= std::clamp(std::min(0.5f - std::abs(qx), 0.5f - std::abs(qy)) * 4.0f / std::max(b.Falloff, 1e-3f), 0.0f, 1.0f);
                    }
                    const int v = (int)(std::clamp(m, 0.0f, 1.0f) * 255.0f);
                    dl->AddRectFilled(ImVec2(p0.x + x * cell, p0.y + y * cell), ImVec2(p0.x + (x + 1) * cell, p0.y + (y + 1) * cell), IM_COL32(v, v, v, 255));
                }
            ImGui::Dummy(ImVec2(size, size));
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Select it as the Brush Shape in the Terrain panel; Raise, Lower, Smooth, Flatten, Noise and Paint use its shape.");
    }
}
