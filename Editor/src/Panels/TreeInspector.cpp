#include "TreeInspector.h"

#include "Scene/TreeCreator.h"
#include "Assets/AssetUI.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    namespace
    {
        bool IsBranch(const TreeComponent& tree, int index)
        {
            return index >= 0 && index < (int)tree.Groups.size() && tree.Groups[index].Type == TreeGroup::Kind::Branch;
        }

        std::string GroupLabel(const TreeComponent& tree, int index)
        {
            const TreeGroup& g = tree.Groups[index];
            std::string name = g.Name.empty() ? (g.Type == TreeGroup::Kind::Leaf ? "Leaf Group" : "Branch Group") : g.Name;
            return std::string(g.Type == TreeGroup::Kind::Leaf ? ICON_FA_LEAF "  " : ICON_FA_CODE_BRANCH "  ") + name;
        }

        // Grup hiyerarşisi (kökten derinlik öncelikli): seçilebilir satırlar, girintili
        void DrawHierarchy(TreeComponent& tree)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            const float rowH = ImGui::GetTextLineHeightWithSpacing();
            const float height = std::min(rowH * (float)(tree.Groups.size() + 1) + 10.0f, rowH * 10.0f);
            ImGui::BeginChild("##treegroups", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
            ImGui::TextDisabled(ICON_FA_TREE "  Tree Root Node");
            std::function<void(int, int)> rows = [&](int parent, int depth) {
                for (int i = 0; i < (int)tree.Groups.size(); ++i)
                {
                    const int p = tree.Groups[i].Parent >= 0 && tree.Groups[i].Parent < i ? tree.Groups[i].Parent : -1;
                    if (p != parent)
                        continue;
                    ImGui::PushID(i);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f * (float)(depth + 1));
                    const std::string label = GroupLabel(tree, i) + "   (" + std::to_string(tree.Groups[i].Frequency) + ")";
                    if (ImGui::Selectable(label.c_str(), tree.SelectedGroup == i))
                        tree.SelectedGroup = i;
                    ImGui::PopID();
                    rows(i, depth + 1);
                }
            };
            rows(-1, 0);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void DrawToolbar(TreeComponent& tree)
        {
            const int sel = tree.SelectedGroup;
            const bool selBranch = IsBranch(tree, sel);
            // Yeni dal grubu: seçili dal grubunun altına (seçim yaprak ya da yoksa kökten)
            if (ImGui::Button(ICON_FA_CODE_BRANCH " Add Branch Group"))
            {
                const int parent = selBranch ? sel : -1;
                tree.Groups.push_back(TreeCreator::MakeGroup(TreeGroup::Kind::Branch, parent, "Branch Group " + std::to_string(tree.Groups.size())));
                tree.SelectedGroup = (int)tree.Groups.size() - 1;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!selBranch);
            if (ImGui::Button(ICON_FA_LEAF " Add Leaf Group"))
            {
                tree.Groups.push_back(TreeCreator::MakeGroup(TreeGroup::Kind::Leaf, sel, "Leaf Group " + std::to_string(tree.Groups.size())));
                tree.SelectedGroup = (int)tree.Groups.size() - 1;
            }
            ImGui::EndDisabled();
            if (!selBranch && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Select a branch group: leaves grow on branches.");
            const bool valid = sel >= 0 && sel < (int)tree.Groups.size();
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(ICON_FA_CLONE " Duplicate"))
            {
                const int copy = TreeCreator::DuplicateGroup(tree, sel);
                if (copy >= 0)
                    tree.SelectedGroup = copy;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TRASH " Delete"))
                TreeCreator::RemoveGroup(tree, sel);
            ImGui::EndDisabled();
            if (valid && ImGui::IsItemHovered())
                ImGui::SetTooltip("Deletes the group and the groups that grow on it.");
        }

        void DrawGroup(TreeComponent& tree)
        {
            if (tree.SelectedGroup < 0 || tree.SelectedGroup >= (int)tree.Groups.size())
                return;
            TreeGroup& g = tree.Groups[tree.SelectedGroup];
            const bool leaf = g.Type == TreeGroup::Kind::Leaf;
            UI::SubHeader(leaf ? "Leaf Group" : "Branch Group");
            UI::InputText("Name", g.Name);
            // Üst grup: yalnız bu gruptan önceki dal grupları (sıra korunur: üst her zaman önce)
            {
                std::vector<int> options{ -1 };
                for (int i = 0; i < tree.SelectedGroup; ++i)
                    if (IsBranch(tree, i))
                        options.push_back(i);
                if (leaf)
                    options.erase(options.begin());   // yaprak kökten çıkmaz
                auto nameOf = [&](int i) { return i < 0 ? std::string("Tree Root Node") : GroupLabel(tree, i); };
                UI::BeginProperty("Parent", "The group this one grows on");
                if (ImGui::BeginCombo("##treeparent", nameOf(g.Parent).c_str()))
                {
                    for (int o : options)
                        if (ImGui::Selectable(nameOf(o).c_str(), g.Parent == o))
                            g.Parent = o;
                    ImGui::EndCombo();
                }
                UI::EndProperty();
            }

            UI::SubHeader("Distribution");
            UI::DragInt("Group Seed", g.Seed, 1.0f, 0, 999999, "Random variation of this group only");
            UI::DragInt("Frequency", g.Frequency, 0.2f, 0, 512, g.Parent < 0 ? "Number of trunks" : "How many grow on every parent branch");
            const char* modes[] = { "Random", "Alternate", "Opposite", "Whorled" };
            UI::ComboEnum("Distribution", g.Mode, modes, 4, "How they are placed along the parent");
            if (g.Mode == TreeGroup::Distribution::Whorled)
                UI::DragInt("Whorl Step", g.Whorl, 0.1f, 1, 16, "Number in every ring");
            if (g.Mode != TreeGroup::Distribution::Random)
                UI::DragFloat("Twirl", g.Twirl, 0.5f, -180.0f, 180.0f, "%.1f deg", "Extra turn between neighbours");
            if (g.Parent >= 0)
            {
                if (UI::Vec2("Range", g.Range, 0.005f, "Start and end along the parent (0 base, 1 tip)"))
                    g.Range = glm::clamp(g.Range, 0.0f, 1.0f);
            }
            UI::Vec2("Growth Scale", g.GrowthScale, 0.005f, "Size at the base and at the tip of the parent");
            UI::Vec2("Growth Angle", g.GrowthAngle, 0.25f, g.Parent < 0 ? "Lean of the trunks (degrees)" : "Angle from the parent (degrees) at its base and tip");

            UI::SubHeader("Geometry");
            {
                UI::BeginProperty(leaf ? "Leaf Material" : "Branch Material", "Empty = default bark / leaves");
                std::string mat = g.Material;
                if (AssetUI::AssetSlot("##treematerial", "Material", { ".cqmat" }, mat))
                    g.Material = mat;
                UI::EndProperty();
            }

            UI::SubHeader("Shape");
            if (!leaf)
            {
                UI::Vec2("Length", g.Length, 0.01f, g.RelativeLength ? "Min / max, relative to the parent's length" : "Min / max (meters)");
                g.Length = glm::max(g.Length, glm::vec2(0.0f));
                if (g.Parent >= 0)
                    UI::Checkbox("Relative Length", g.RelativeLength, "Length is a fraction of the parent's length");
                UI::DragFloat("Radius", g.Radius, 0.002f, 0.002f, 5.0f, "%.3f m");
                UI::SliderFloat("Cap Taper", g.CapTaper, 0.0f, 1.0f, "%.2f", "Tip radius / base radius");
                UI::SliderFloat("Crinkliness", g.Crinkliness, 0.0f, 1.0f, "%.2f");
                UI::SliderFloat("Seek Sun", g.SeekSun, -1.0f, 1.0f, "%.2f", "Bend up towards the sun (negative: droop)");
                UI::SliderFloat("Noise", g.Noise, 0.0f, 1.0f, "%.2f", "Bark surface bumps");
                if (g.Parent < 0)
                {
                    UI::SliderFloat("Flare Radius", g.FlareRadius, 0.0f, 3.0f, "%.2f", "Widening at the root");
                    UI::SliderFloat("Flare Height", g.FlareHeight, 0.0f, 1.0f, "%.2f", "Height of the root flare (fraction of the length)");
                }
            }
            else
            {
                UI::Vec2("Size", g.Size, 0.01f, "Min / max (meters)");
                g.Size = glm::max(g.Size, glm::vec2(0.01f));
                const char* shapes[] = { "Plane", "Cross", "Tri-Cross" };
                UI::ComboEnum("Geometry Mode", g.Shape, shapes, 3, "Crossed planes look full from every side");
                UI::SliderFloat("Perpendicular Align", g.PerpendicularAlign, 0.0f, 1.0f, "%.2f", "Turn the leaves to face along the branch");
                UI::SliderFloat("Horizontal Align", g.HorizontalAlign, 0.0f, 1.0f, "%.2f", "Lay the leaves flat, facing up");
            }
        }
    }

    void DrawTreeInspector(Entity entity)
    {
        if (!entity || !entity.HasComponent<TreeComponent>())
            return;
        auto& tree = entity.GetComponent<TreeComponent>();
        tree.SelectedGroup = std::clamp(tree.SelectedGroup, 0, std::max(0, (int)tree.Groups.size() - 1));
        UI::SubHeader("Groups");
        DrawHierarchy(tree);
        DrawToolbar(tree);
        DrawGroup(tree);
        ImGui::Spacing();
        ImGui::TextDisabled("%u vertices, %u triangles", tree.VertexCount, tree.TriangleCount);
    }
}
