#include "LensFlareInspector.h"

#include "Scene/Billboards.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <filesystem>

namespace Conqueror::Editor::LensFlareInspector
{
    void Draw(const std::string& path)
    {
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_STAR);
        ImGui::SameLine();
        ImGui::TextUnformatted(std::filesystem::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Lens Flare");
        ImGui::Separator();
        std::vector<LensFlareElement> elements;
        if (!LensFlareAssets::Load(path, elements))
        {
            UI::InfoBox("The lens flare could not be read.", UI::InfoType::Error);
            return;
        }
        bool changed = false;
        int remove = -1, up = -1;
        static const char* shapes[] = { "Glow", "Ring", "Hexagon", "Circle", "Streak" };
        for (int i = 0; i < (int)elements.size(); ++i)
        {
            auto& el = elements[(size_t)i];
            ImGui::PushID(i);
            const std::string title = "Element " + std::to_string(i) + "  (" + shapes[std::clamp((int)el.Shape, 0, 4)] + ")";
            const bool open = UI::BeginSection(title.c_str(), true);
            if (open)
            {
                int shape = (int)el.Shape;
                if (UI::Combo("Shape", shape, shapes, 5)) { el.Shape = (LensFlareShape)shape; changed = true; }
                changed |= UI::Color3("Color", el.Color);
                changed |= UI::DragFloat("Size", el.Size, 0.005f, 0.0f, 4.0f, "%.3f", "Fraction of the screen height.");
                changed |= UI::DragFloat("Intensity", el.Intensity, 0.01f, 0.0f, 20.0f);
                changed |= UI::SliderFloat("Position", el.Position, -1.0f, 2.0f, "%.2f",
                                           "Along the flare axis: 0 = on the light, 0.5 = screen centre, 1 = mirrored across the centre.");
                changed |= UI::SliderInt("Count", el.Count, 1, 16, "Repeats (ghost chains).");
                if (el.Count > 1)
                    changed |= UI::DragFloat("Spacing", el.Spacing, 0.005f, -2.0f, 2.0f);
                changed |= UI::DragFloat("Stretch", el.Stretch, 0.01f, 0.01f, 40.0f, "%.2f", "Horizontal stretch (streaks).");
                if (ImGui::SmallButton(ICON_FA_ARROW_UP " Up") && i > 0) up = i;
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH " Remove")) remove = i;
                UI::EndSection();
            }
            ImGui::PopID();
        }
        if (up > 0) { std::swap(elements[(size_t)up], elements[(size_t)up - 1]); changed = true; }
        if (remove >= 0) { elements.erase(elements.begin() + remove); changed = true; }
        if (UI::ButtonFullWidth(ICON_FA_PLUS "  Add Element", 26.0f))
        {
            elements.push_back({});
            changed = true;
        }
        if (changed)
            LensFlareAssets::Save(path, elements);
        ImGui::Spacing();
        ImGui::TextWrapped("Assign it to a Lens Flare component (Lens Flare Data) on a light or object.");
    }
}
