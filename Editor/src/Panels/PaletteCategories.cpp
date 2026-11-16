#include "PaletteCategories.h"

#include "Core/Project/Project.h"
#include "Panels/Shader/ShaderEditorPanel.h"
#include "Scripting/Visual/VisualGraph.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace Conqueror::Editor::Palette
{
    namespace fs = std::filesystem;

    namespace
    {
        struct State
        {
            int Category = Tiles;
            int LastFocus = -1;            // son odaklanan düzenleyicinin kategorisi
            char Search[128] = {};
            bool Thumbnails = false;       // büyük kutu görünümü
            int Source = 0;                // 0 hepsi, 1 Library, 2 User Created
            std::set<std::string> Favorites;
            std::vector<std::string> Recent;
            std::vector<std::string> UserItems;
            bool UserScanned = false;
            fs::path LoadedFor;
        };
        State& S()
        {
            static auto& s = *new State();
            return s;
        }

        fs::path PrefsFile() { return Project::GetActiveProjectDirectory() / "Library" / "palette.yaml"; }

        // Favoriler ve son kullanılanlar projeyle (Library/palette.yaml, sürüm kontrolüne girmez)
        void LoadPrefs()
        {
            State& s = S();
            const fs::path dir = Project::GetActiveProjectDirectory();
            if (dir == s.LoadedFor)
                return;
            s.LoadedFor = dir;
            s.Favorites.clear();
            s.Recent.clear();
            std::ifstream in(PrefsFile());
            std::string line, section;
            while (std::getline(in, line))
            {
                if (line == "Favorites:" || line == "Recent:")
                    section = line;
                else if (line.rfind("  - ", 0) == 0)
                    (section == "Favorites:" ? (void)s.Favorites.insert(line.substr(4)) : s.Recent.push_back(line.substr(4)));
            }
        }

        void SavePrefs()
        {
            State& s = S();
            std::error_code ec;
            fs::create_directories(PrefsFile().parent_path(), ec);
            std::ofstream out(PrefsFile());
            out << "Favorites:\n";
            for (const auto& f : s.Favorites)
                out << "  - " << f << "\n";
            out << "Recent:\n";
            for (const auto& r : s.Recent)
                out << "  - " << r << "\n";
        }

        std::string Lower(std::string v)
        {
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return v;
        }

        const char* UIIcon(const std::string& item)
        {
            if (item.rfind("prefab:", 0) == 0) return ICON_FA_CUBES_STACKED;
            if (item == "Text") return ICON_FA_FONT;
            if (item == "Image" || item == "Raw Image") return ICON_FA_IMAGE;
            if (item == "Button") return ICON_FA_HAND_POINTER;
            if (item == "Toggle") return ICON_FA_SQUARE_CHECK;
            if (item == "Slider" || item == "Scrollbar") return ICON_FA_SLIDERS;
            if (item == "Dropdown") return ICON_FA_CARET_DOWN;
            if (item == "Input Field") return ICON_FA_I_CURSOR;
            if (item == "Scroll View") return ICON_FA_SCROLL;
            if (item == "Layout Group") return ICON_FA_TABLE_COLUMNS;
            return ICON_FA_SQUARE;
        }

        std::string DisplayName(const std::string& item)
        {
            return item.rfind("prefab:", 0) == 0 ? fs::path(item.substr(7)).stem().string() : item;
        }

        // Bir öğe kutusu: tık ekler, sürükleme bırakılacak düzenleyiciye taşır, sağ tık favori
        bool ItemButton(const std::string& id, const std::string& label, const char* icon, bool enabled, const char* payloadType,
                        const std::string& payload, const std::string& tooltip)
        {
            State& s = S();
            bool clicked = false;
            ImGui::PushID(id.c_str());
            ImGui::BeginDisabled(!enabled);
            if (s.Thumbnails)
            {
                ImGui::BeginGroup();
                clicked = ImGui::Button((std::string(icon) + "##b").c_str(), ImVec2(72, 56));
                const float w = ImGui::CalcTextSize(label.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (72 - w) * 0.5f));
                ImGui::TextUnformatted(label.c_str());
                ImGui::EndGroup();
            }
            else
                clicked = ImGui::Selectable((std::string(icon) + "  " + label + (s.Favorites.count(id) ? "  " ICON_FA_STAR : "")).c_str());
            ImGui::EndDisabled();
            if (enabled && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(payloadType, payload.c_str(), payload.size() + 1);
                ImGui::Text("%s  %s", icon, label.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) && !tooltip.empty())
                ImGui::SetTooltip("%s", tooltip.c_str());
            if (ImGui::BeginPopupContextItem("##fav"))
            {
                const bool fav = s.Favorites.count(id) > 0;
                if (ImGui::MenuItem(fav ? ICON_FA_STAR " Remove from Favorites" : ICON_FA_STAR " Add to Favorites"))
                {
                    if (fav) s.Favorites.erase(id); else s.Favorites.insert(id);
                    SavePrefs();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            if (s.Thumbnails)
            {
                const float next = ImGui::GetItemRectMax().x + 80.0f;
                if (next < ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x)
                    ImGui::SameLine();
            }
            return clicked && enabled;
        }

        void DrawUIItems()
        {
            State& s = S();
            const bool open = GetHooks().UIEditorOpen && GetHooks().UIEditorOpen();
            const std::string q = Lower(s.Search);
            auto draw = [&](const std::vector<std::string>& items, const char* header) {
                bool any = false;
                for (const auto& item : items)
                {
                    if (!q.empty() && Lower(DisplayName(item)).find(q) == std::string::npos)
                        continue;
                    if (!any)
                    {
                        ImGui::SeparatorText(header);
                        any = true;
                    }
                    const std::string tip = item.rfind("prefab:", 0) == 0 ? "Your UI prefab " + item.substr(7) : "Add a " + item + " to the canvas";
                    if (ItemButton(item, DisplayName(item), UIIcon(item), open, "UI_PALETTE_ITEM", item,
                                   open ? tip + " (click, or drag it onto the UI Editor)" : "Open the UI Editor first"))
                    {
                        GetHooks().AddUI(item);
                        NoteUsed(item);
                    }
                }
                if (any && s.Thumbnails)
                    ImGui::NewLine();
            };
            std::vector<std::string> fav(s.Favorites.begin(), s.Favorites.end());
            fav.erase(std::remove_if(fav.begin(), fav.end(), [](const std::string& f) { return f.rfind("node:", 0) == 0 || f.rfind("shader:", 0) == 0; }), fav.end());
            std::vector<std::string> recent;
            for (const auto& r : s.Recent)
                if (r.rfind("node:", 0) != 0 && r.rfind("shader:", 0) != 0)
                    recent.push_back(r);
            if (!fav.empty())
                draw(fav, ICON_FA_STAR " Favorites");
            if (!recent.empty())
                draw(recent, ICON_FA_CLOCK_ROTATE_LEFT " Recently Used");
            if (s.Source != 2)
                draw(LibraryUIItems(), ICON_FA_BOOK " Library");
            if (s.Source != 1)
            {
                const auto user = UserUIItems();
                if (user.empty() && q.empty())
                {
                    ImGui::SeparatorText(ICON_FA_USER " User Created");
                    ImGui::TextDisabled("Your UI prefabs appear here (make one from a UI element: Create Prefab Asset).");
                }
                else
                    draw(user, ICON_FA_USER " User Created");
            }
        }

        void DrawBlueprintNodes()
        {
            State& s = S();
            const bool open = GetHooks().BlueprintOpen && GetHooks().BlueprintOpen();
            const std::string q = Lower(s.Search);
            std::map<std::string, std::vector<const Visual::NodeDef*>> byCategory;
            for (const auto& d : Visual::NodeLibrary::All())
                if (q.empty() || Lower(d.Title).find(q) != std::string::npos || Lower(d.Category).find(q) != std::string::npos)
                    byCategory[d.Category.empty() ? "Other" : d.Category].push_back(&d);
            for (auto& [category, defs] : byCategory)
            {
                // Arama yokken kategoriler kapalı başlar (yüzlerce düğüm)
                if (!q.empty())
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (!ImGui::CollapsingHeader((category + " (" + std::to_string(defs.size()) + ")").c_str()))
                    continue;
                for (const Visual::NodeDef* d : defs)
                    if (ItemButton("node:" + d->Id, d->Title, ICON_FA_DIAGRAM_PROJECT, open, "VS_PALETTE_NODE", d->Id,
                                   open ? d->Description : "Open a Visual Script graph first"))
                    {
                        GetHooks().AddBlueprintNode(d->Id);
                        NoteUsed("node:" + d->Id);
                    }
                if (s.Thumbnails)
                    ImGui::NewLine();
            }
        }

        void DrawShaderNodes()
        {
            State& s = S();
            const bool open = GetHooks().ShaderOpen && GetHooks().ShaderOpen();
            const std::string q = Lower(s.Search);
            for (int t = 1; t < ShaderEditorPanel::NodeTypeCount(); ++t)
            {
                const char* name = ShaderEditorPanel::NodeTypeName(t);
                if (!name || std::strcmp(name, "Unknown") == 0 || std::strcmp(name, "Master Output") == 0 || std::strcmp(name, "MasterOutput") == 0)
                    continue;
                if (!q.empty() && Lower(name).find(q) == std::string::npos)
                    continue;
                if (ItemButton("shader:" + std::to_string(t), name, ICON_FA_CIRCLE_NODES, open, "SHADER_PALETTE_NODE", std::to_string(t),
                               open ? "Add to the Shader Graph" : "Open a material in the Shader Graph editor first"))
                {
                    GetHooks().AddShaderNode(t);
                    NoteUsed("shader:" + std::to_string(t));
                }
            }
            if (s.Thumbnails)
                ImGui::NewLine();
        }
    }

    Hooks& GetHooks()
    {
        static auto& h = *new Hooks();
        return h;
    }

    int GetCategory() { return S().Category; }
    void SetCategory(int category) { S().Category = std::clamp(category, 0, (int)Count - 1); }

    void FollowFocus()
    {
        // Odaktaki pencerenin kökü: UI Editor -> UI, Visual Script -> Blueprint, Shader Graph -> Shader
        ImGuiContext* g = ImGui::GetCurrentContext();
        if (!g || !g->NavWindow)
            return;
        const std::string name = g->NavWindow->RootWindow ? g->NavWindow->RootWindow->Name : g->NavWindow->Name;
        int focus = -1;
        if (name.find("###UI Editor") != std::string::npos)
            focus = UI;
        else if (name.find("Visual Script") != std::string::npos)
            focus = Blueprint;
        else if (name.find("Shader Graph Editor") != std::string::npos)
            focus = Shader;
        State& s = S();
        if (focus >= 0 && focus != s.LastFocus)
            s.Category = focus;
        if (focus >= 0)
            s.LastFocus = focus;
    }

    std::vector<std::string> LibraryUIItems()
    {
        return { "Panel", "Image", "Raw Image", "Text", "Button", "Toggle", "Slider", "Scrollbar", "Dropdown", "Input Field", "Scroll View", "Layout Group" };
    }

    std::vector<std::string> UserUIItems(bool rescan)
    {
        State& s = S();
        if (s.UserScanned && !rescan)
            return s.UserItems;
        s.UserScanned = true;
        s.UserItems.clear();
        const fs::path project = Project::GetActiveProjectDirectory();
        std::error_code ec;
        if (project.empty() || !fs::is_directory(project / "Assets", ec))
            return s.UserItems;
        // UI prefab'leri: kök öğesi RectTransform olan .cqprefab dosyaları
        for (auto it = fs::recursive_directory_iterator(project / "Assets", fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec || !it->is_regular_file(ec) || it->path().extension() != ".cqprefab")
                continue;
            std::ifstream in(it->path());
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (text.find("RectTransformComponent") != std::string::npos)
                s.UserItems.push_back("prefab:" + fs::relative(it->path(), project, ec).generic_string());
        }
        std::sort(s.UserItems.begin(), s.UserItems.end());
        return s.UserItems;
    }

    void NoteUsed(const std::string& item)
    {
        State& s = S();
        s.Recent.erase(std::remove(s.Recent.begin(), s.Recent.end(), item), s.Recent.end());
        s.Recent.insert(s.Recent.begin(), item);
        if (s.Recent.size() > 8)
            s.Recent.resize(8);
        SavePrefs();
    }

    bool DrawCategoryBar()
    {
        State& s = S();
        LoadPrefs();
        const Hooks& h = GetHooks();
        struct Button { const char* Label; int Cat; bool Enabled; };
        const Button buttons[] = {
            { ICON_FA_OBJECT_GROUP " UI", UI, h.UIEditorOpen && h.UIEditorOpen() },
            { ICON_FA_DIAGRAM_PROJECT " Blueprint", Blueprint, h.BlueprintOpen && h.BlueprintOpen() },
            { ICON_FA_CIRCLE_NODES " Shader", Shader, h.ShaderOpen && h.ShaderOpen() },
            { ICON_FA_TABLE_CELLS " Tiles", Tiles, true },
        };
        for (int i = 0; i < 4; ++i)
        {
            if (i > 0)
                ImGui::SameLine();
            const Button& b = buttons[i];
            const bool active = s.Category == b.Cat;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::BeginDisabled(!b.Enabled && !active);
            if (ImGui::Button(b.Label))
                s.Category = b.Cat;
            ImGui::EndDisabled();
            if (active)
                ImGui::PopStyleColor();
            if (!b.Enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", b.Cat == UI ? "Open the UI Editor to use UI elements" : b.Cat == Blueprint ? "Open a Visual Script graph"
                                                                                                               : "Open a material in the Shader Graph editor");
        }
        if (s.Category == Tiles)
        {
            ImGui::Separator();
            return true;
        }
        // Arama + görünüm + (UI) kaynak süzgeci
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - (s.Category == UI ? 170.0f : 40.0f)));
        ImGui::InputTextWithHint("##palsearch", ICON_FA_MAGNIFYING_GLASS "  Search", s.Search, sizeof(s.Search));
        ImGui::SameLine();
        if (ImGui::Button(s.Thumbnails ? ICON_FA_LIST : ICON_FA_TABLE_CELLS_LARGE))
            s.Thumbnails = !s.Thumbnails;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(s.Thumbnails ? "List view" : "Large view");
        if (s.Category == UI)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            static const char* sources[] = { "All", "Library", "User Created" };
            ImGui::Combo("##palsource", &s.Source, sources, 3);
        }
        ImGui::Separator();
        return false;
    }

    void DrawCategory()
    {
        ImGui::BeginChild("##palitems");
        switch (S().Category)
        {
            case UI: DrawUIItems(); break;
            case Blueprint: DrawBlueprintNodes(); break;
            case Shader: DrawShaderNodes(); break;
            default: break;
        }
        ImGui::EndChild();
    }
}
