#include "AvatarMaskInspector.h"

#include "Assets/AssetUI.h"
#include "UI/EditorUI.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include "Core/Animation/Animator.h"
#include "Core/Animation/AvatarMask.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <filesystem>
#include <unordered_map>

namespace Conqueror::Editor::AvatarMaskInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        struct EditSession
        {
            std::string Path, Before;
            bool Open = false;
        } s_Edit;

        void Commit(bool force)
        {
            if (!s_Edit.Open || (!force && ImGui::IsAnyItemActive()))
                return;
            s_Edit.Open = false;
            std::string after = AssetFileCommand::ReadFile(s_Edit.Path);
            if (after != s_Edit.Before)
                UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(s_Edit.Path, s_Edit.Before, std::move(after),
                                                                                    "Edit " + fs::path(s_Edit.Path).filename().string()));
        }

        // İnsan figürü (Unity Avatar Mask): karaktere önden bakılır, sol kol görüntünün sağında. Tıkla: bölümü aç / kapat
        bool HumanoidFigure(AvatarMask& mask)
        {
            bool changed = false;
            const float w = std::min(ImGui::GetContentRegionAvail().x, 260.0f), h = 300.0f;
            const ImVec2 o(ImGui::GetCursorScreenPos().x + (ImGui::GetContentRegionAvail().x - w) * 0.5f, ImGui::GetCursorScreenPos().y);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(o, ImVec2(o.x + w, o.y + h), IM_COL32(28, 29, 33, 255), 6.0f);
            const float cx = o.x + w * 0.5f;
            struct Shape
            {
                HumanPart Part;
                ImVec2 Min, Max;
                bool Circle;
            };
            const Shape shapes[] = {
                { HumanPart::Head, { cx - 20, o.y + 14 }, { cx + 20, o.y + 54 }, true },
                { HumanPart::Body, { cx - 30, o.y + 60 }, { cx + 30, o.y + 150 }, false },
                // Ekranın solu karakterin sağı
                { HumanPart::RightArm, { cx - 62, o.y + 62 }, { cx - 36, o.y + 150 }, false },
                { HumanPart::LeftArm, { cx + 36, o.y + 62 }, { cx + 62, o.y + 150 }, false },
                { HumanPart::RightHand, { cx - 66, o.y + 154 }, { cx - 34, o.y + 184 }, true },
                { HumanPart::LeftHand, { cx + 34, o.y + 154 }, { cx + 66, o.y + 184 }, true },
                { HumanPart::RightLeg, { cx - 28, o.y + 156 }, { cx - 4, o.y + 262 }, false },
                { HumanPart::LeftLeg, { cx + 4, o.y + 156 }, { cx + 28, o.y + 262 }, false },
                { HumanPart::Root, { cx - 40, o.y + 270 }, { cx + 40, o.y + 290 }, false },
            };
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            ImGui::InvisibleButton("##figure", ImVec2(ImGui::GetContentRegionAvail().x, h));
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            int hover = -1;
            for (int i = 0; i < (int)(sizeof(shapes) / sizeof(shapes[0])); ++i)
            {
                const Shape& s = shapes[i];
                const bool on = mask.Parts[(int)s.Part];
                const bool inside = mouse.x >= s.Min.x && mouse.x <= s.Max.x && mouse.y >= s.Min.y && mouse.y <= s.Max.y;
                if (inside && ImGui::IsItemHovered())
                    hover = i;
                ImU32 col = on ? IM_COL32(90, 200, 110, 255) : IM_COL32(210, 80, 80, 255);
                if (hover == i)
                    col = on ? IM_COL32(130, 235, 150, 255) : IM_COL32(240, 120, 120, 255);
                if (s.Circle)
                    dl->AddCircleFilled(ImVec2((s.Min.x + s.Max.x) * 0.5f, (s.Min.y + s.Max.y) * 0.5f), (s.Max.x - s.Min.x) * 0.5f, col, 24);
                else if (s.Part == HumanPart::Root)
                    dl->AddRectFilled(s.Min, s.Max, col, 8.0f);
                else
                    dl->AddRectFilled(s.Min, s.Max, col, 6.0f);
            }
            if (hover >= 0)
            {
                const HumanPart part = shapes[hover].Part;
                ImGui::SetTooltip("%s: %s (click to toggle)", AvatarMasks::PartName(part), mask.Parts[(int)part] ? "included" : "excluded");
                if (clicked)
                {
                    mask.Parts[(int)part] = !mask.Parts[(int)part];
                    changed = true;
                }
            }
            return changed;
        }
    }

    void Draw(const std::string& path)
    {
        static std::string s_Path;
        static fs::file_time_type s_Time{};
        static AvatarMask s_Mask;
        static bool s_Loaded = false;
        static std::string s_Error;
        static char s_Filter[64] = {};
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (s_Edit.Open && s_Edit.Path != path)
            Commit(true);
        if (s_Path != path || s_Time != time)
        {
            s_Path = path;
            s_Time = time;
            s_Loaded = AvatarMasks::Load(path, s_Mask, &s_Error);
        }

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_PERSON);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Avatar Mask");
        ImGui::Separator();
        if (!s_Loaded)
        {
            UI::InfoBox(("The mask could not be read: " + s_Error).c_str(), UI::InfoType::Error);
            return;
        }

        bool changed = false;
        if (ImGui::CollapsingHeader("Humanoid", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginDisabled(s_Mask.UsesTransforms());
            changed |= HumanoidFigure(s_Mask);
            if (ImGui::SmallButton("All"))
            {
                for (auto& p : s_Mask.Parts)
                    p = true;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("None"))
            {
                for (auto& p : s_Mask.Parts)
                    p = false;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Upper Body"))
            {
                for (int i = 0; i < (int)HumanPart::Count; ++i)
                {
                    const HumanPart p = (HumanPart)i;
                    s_Mask.Parts[i] = p == HumanPart::Body || p == HumanPart::Head || p == HumanPart::LeftArm || p == HumanPart::RightArm ||
                                      p == HumanPart::LeftHand || p == HumanPart::RightHand;
                }
                changed = true;
            }
            ImGui::EndDisabled();
            if (s_Mask.UsesTransforms())
                ImGui::TextDisabled("The Transform list below decides; clear it to use the body parts.");
        }

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            UI::BeginProperty("Use Skeleton From", "A model whose skeleton lists the bones to choose one by one.");
            std::string model = s_Mask.SourceModel;
            if (AssetUI::AssetSlot("##model", "Model", { ".fbx", ".glb", ".gltf", ".obj", ".dae" }, model))
            {
                s_Mask.SourceModel = model;
                changed = true;
            }
            UI::EndProperty();
            std::shared_ptr<Model> m = s_Mask.SourceModel.empty() ? nullptr : ModelLoader::Load(SceneSerializer::ResolveAssetPath(s_Mask.SourceModel));
            const auto* nodes = m && m->SkeletonData ? &Animator::Flatten(*m->SkeletonData) : nullptr;
            ImGui::BeginDisabled(!nodes);
            if (ImGui::Button(ICON_FA_FILE_IMPORT "  Import Skeleton", ImVec2(-1, 0)) && nodes)
            {
                // Bütün kemikler, var olan seçimler korunarak
                std::unordered_map<std::string, bool> previous(s_Mask.Transforms.begin(), s_Mask.Transforms.end());
                s_Mask.Transforms.clear();
                for (const auto& n : *nodes)
                    if (n.Node)
                    {
                        auto it = previous.find(n.Node->Name);
                        s_Mask.Transforms.push_back({ n.Node->Name, it == previous.end() ? true : it->second });
                    }
                changed = true;
            }
            ImGui::EndDisabled();
            if (!s_Mask.SourceModel.empty() && !nodes)
                ImGui::TextDisabled("The model has no skeleton.");
            if (s_Mask.UsesTransforms())
            {
                if (ImGui::SmallButton(ICON_FA_TRASH " Clear Transforms"))
                {
                    s_Mask.Transforms.clear();
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##boneFilter", ICON_FA_MAGNIFYING_GLASS " Filter bones", s_Filter, sizeof(s_Filter));
                // Ağaç: kemik derinliği modelden (varsa); bir kemiği değiştirmek altını da değiştirir (Unity gibi)
                std::unordered_map<std::string, int> depth, parentOf;
                if (nodes)
                    for (size_t i = 0; i < nodes->size(); ++i)
                    {
                        const auto& n = (*nodes)[i];
                        if (!n.Node)
                            continue;
                        const int d = n.Parent >= 0 && (*nodes)[(size_t)n.Parent].Node ? depth[(*nodes)[(size_t)n.Parent].Node->Name] + 1 : 0;
                        depth[n.Node->Name] = d;
                    }
                ImGui::BeginChild("##bones", ImVec2(0, 260.0f), ImGuiChildFlags_Borders);
                for (size_t i = 0; i < s_Mask.Transforms.size(); ++i)
                {
                    auto& [name, on] = s_Mask.Transforms[i];
                    if (s_Filter[0] && name.find(s_Filter) == std::string::npos)
                        continue;
                    const int d = depth.count(name) ? depth[name] : 0;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (s_Filter[0] ? 0.0f : d * 12.0f));
                    bool v = on;
                    if (ImGui::Checkbox((name + "##b" + std::to_string(i)).c_str(), &v))
                    {
                        on = v;
                        // Alt ağaç: sonraki daha derin kemikler
                        for (size_t j = i + 1; j < s_Mask.Transforms.size(); ++j)
                        {
                            const int dj = depth.count(s_Mask.Transforms[j].first) ? depth[s_Mask.Transforms[j].first] : 0;
                            if (dj <= d)
                                break;
                            s_Mask.Transforms[j].second = v;
                        }
                        changed = true;
                    }
                }
                ImGui::EndChild();
            }
        }

        if (changed)
        {
            if (!s_Edit.Open)
            {
                s_Edit.Path = path;
                s_Edit.Before = AssetFileCommand::ReadFile(path);
                s_Edit.Open = true;
            }
            AvatarMasks::Save(path, s_Mask);
            s_Time = fs::last_write_time(path, ec);
        }
        Commit(false);
        ImGui::Spacing();
        ImGui::TextWrapped("Assign it to an Animator Controller layer: Animator window > layer " ICON_FA_GEAR " > Mask.");
    }
}
