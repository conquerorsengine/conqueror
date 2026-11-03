#include "DataAssetInspector.h"

#include "Assets/AssetUI.h"
#include "Assets/ScriptFieldUI.h"
#include "Core/Project/Project.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ObjectPicker.h"
#include "Scripting/ScriptRuntime.h"
#include "UndoRedo/AssetFileCommand.h"
#include "UndoRedo/UndoRedoManager.h"
#include "Scripting/DataAssets.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace Conqueror::Editor::DataAssetInspector
{
    namespace fs = std::filesystem;

    namespace
    {
        // Düzenleme oturumu (geri al): ilk değişiklikte dosyanın önceki metni alınır; sürükleme / yazma bitince (etkin öğe
        // kalmayınca) tek bir geri alma adımı olarak eklenir (kaydırıcı sürüklemesi her karede ayrı adım olmaz)
        struct EditSession
        {
            std::string Path, Before;
            bool Open = false;
        } s_Edit;

        void CommitEdit(bool force)
        {
            if (!s_Edit.Open || (!force && ImGui::IsAnyItemActive()))
                return;
            s_Edit.Open = false;
            std::string after = AssetFileCommand::ReadFile(s_Edit.Path);
            if (after != s_Edit.Before)
                UndoRedoManager::Get().Execute(std::make_shared<AssetFileCommand>(
                    s_Edit.Path, s_Edit.Before, std::move(after), "Edit " + fs::path(s_Edit.Path).filename().string()));
        }
    }

    void Draw(const std::string& path)
    {
        struct State
        {
            std::string Path;
            fs::file_time_type Time{};
            DataAssets::DataAssetFile File;
            bool Loaded = false;
        };
        static State st;
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (st.Path != path || st.Time != time)
        {
            st.Path = path;
            st.Time = time;
            st.Loaded = DataAssets::Load(path, st.File);
        }

        ImGui::TextColored(UI::Colors::Accent, ICON_FA_DATABASE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Data Asset%s%s", st.File.Class.empty() ? "" : " - ", st.File.Class.c_str());
        ImGui::Separator();
        if (!st.Loaded)
        {
            UI::InfoBox("The file could not be read.", UI::InfoType::Error);
            return;
        }

        // Başka bir varlığa geçildiyse açık düzenleme oturumu önce geri alma adımı olur
        if (s_Edit.Open && s_Edit.Path != path)
            CommitEdit(true);
        auto save = [&]() {
            if (!s_Edit.Open)
            {
                s_Edit.Path = path;
                s_Edit.Before = AssetFileCommand::ReadFile(path);
                s_Edit.Open = true;
            }
            DataAssets::Save(path, st.File);
            st.Time = fs::last_write_time(path, ec);
        };
        struct CommitAtEnd
        {
            ~CommitAtEnd() { CommitEdit(false); }
        } commitAtEnd;
        // Betiğin DataAsset sınıfları (Class seçimi ve betik atanınca)
        auto classesOf = [](const std::string& script) { return DataAssets::DataClassesOf(script); };
        auto assignScript = [&](const std::string& picked) {
            const std::string rel = AssetUI::ToProjectRelative(picked);
            const auto classes = classesOf(rel);
            st.File.Script = rel;
            if (std::find(classes.begin(), classes.end(), st.File.Class) == classes.end())
                st.File.Class = classes.empty() ? fs::path(rel).stem().string() : classes.front();
            save();
        };

        // Script: Unity ScriptableObject Inspector'ındaki betik alanı (tıklayınca açılır; sürükle-bırak ya da ◎ ile değişir)
        UI::BeginProperty("Script", "The script that defines this asset's DataAsset class. Drop a script here or pick one to change it.");
        {
            std::vector<std::string> scriptExts;
            for (auto* runtime : Scripting::ScriptRuntimes::All())
                for (const auto& e : runtime->GetExtensions())
                    scriptExts.push_back(e);
            for (const char* e : { ".h", ".hpp", ".hh", ".hxx" })
                scriptExts.push_back(e);
            const float h = ImGui::GetFrameHeight();
            const std::string shown = st.File.Script.empty() ? std::string("None (Script)") : fs::path(st.File.Script).filename().string();
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
            if (ImGui::Button((std::string(ICON_FA_FILE_CODE "  ") + shown + "##dataScript").c_str(),
                              ImVec2(ImGui::GetContentRegionAvail().x - h - ImGui::GetStyle().ItemSpacing.x, 0.0f)) &&
                !st.File.Script.empty() && InspectorPanel::s_OpenCodeEditor)
                InspectorPanel::s_OpenCodeEditor(AssetUI::Resolve(st.File.Script).string());
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !st.File.Script.empty())
                ImGui::SetTooltip("Open %s", st.File.Script.c_str());
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    const std::string dropped((const char*)payload->Data, payload->DataSize - 1);
                    if (DataAssets::IsDataScriptFile(dropped))
                        assignScript(dropped);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (std::string picked; ObjectPicker::Button("dataScriptPick", "Script", scriptExts,
                                                         st.File.Script.empty() ? std::string() : AssetUI::Resolve(st.File.Script).string(), picked) &&
                                    !picked.empty())
                assignScript(picked);
        }
        UI::EndProperty();

        // Class: betikte birden çok DataAsset sınıfı varsa seçilir
        const auto classes = st.File.Script.empty() ? std::vector<std::string>() : classesOf(st.File.Script);
        if (classes.size() > 1)
        {
            UI::BeginProperty("Class", "The DataAsset class of the script this asset is made of.");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##dataClass", st.File.Class.c_str()))
            {
                for (const auto& cls : classes)
                    if (ImGui::Selectable(cls.c_str(), cls == st.File.Class) && cls != st.File.Class)
                    {
                        st.File.Class = cls;
                        save();
                    }
                ImGui::EndCombo();
            }
            UI::EndProperty();
        }
        else
            UI::Text("Class", "%s", st.File.Class.c_str());

        // Sınıf yoksa (betik silinmiş, sınıf adı değişmiş ya da derlenemiyor) Unity gibi değerler korunur, düzenlenmez
        if (st.File.Script.empty() || !DataAssets::IsDataClass(st.File.Script, st.File.Class))
        {
            const std::string scriptName = st.File.Script.empty() ? std::string("the script") : fs::path(st.File.Script).filename().string();
            std::string text = "The class " + st.File.Class + " could not be found in " + scriptName +
                               " or does not derive from DataAsset. Fix the script or assign a valid one.";
            if (!st.File.Fields.empty())
                text += " The " + std::to_string(st.File.Fields.size()) + " stored value(s) are kept.";
            UI::InfoBox(text.c_str(), UI::InfoType::Warning);
        }
        else
        {
            std::vector<Scripting::ScriptFieldInfo> fields;
            std::string error;
            DataAssets::ClassFields(st.File.Script, st.File.Class, fields, &error);
            if (!error.empty())
                UI::InfoBox(error.c_str(), UI::InfoType::Error);
            UI::SubHeader("Values");
            if (fields.empty())
                ImGui::TextDisabled("The class has no public fields.");
            else if (ScriptFieldUI::DrawFieldTable("##dataFields", fields, st.File.Fields))
                save();   // Unity'de ScriptableObject değişikliği varlığa yazılır
            // Sınıfta artık olmayan ama dosyada kalan değerler (sınıf bulunduğunda; derleme hatasında silinmez)
            std::vector<std::string> stale;
            if (error.empty())
                for (const auto& [name, v] : st.File.Fields)
                {
                    bool known = false;
                    for (const auto& f : fields)
                        known |= f.Name == name;
                    if (!known)
                        stale.push_back(name);
                }
            if (!stale.empty())
            {
                std::string text = "Stored values for fields " + st.File.Class + " does not have:";
                for (const auto& n : stale)
                    text += " " + n;
                UI::InfoBox(text.c_str(), UI::InfoType::Warning);
                if (ImGui::Button(ICON_FA_BROOM " Remove Unused Values", ImVec2(-1, 0)))
                {
                    for (const auto& n : stale)
                        st.File.Fields.erase(n);
                    save();
                }
            }
        }

        UI::SubHeader("Use in scripts");
        const std::string relative = AssetUI::ToProjectRelative(path);
        const std::string code = "var data = LoadDataAsset(\"" + relative + "\");";
        ImGui::TextWrapped("%s", code.c_str());
        if (ImGui::SmallButton(ICON_FA_COPY " Copy"))
            ImGui::SetClipboardText(code.c_str());
        ImGui::TextWrapped("Or give a script a string field with [Asset(\".cqdata\")] and drop this file on it.");
    }
}
