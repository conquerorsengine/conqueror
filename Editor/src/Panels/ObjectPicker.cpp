#include "ObjectPicker.h"

#include "ContentBrowserPanel.h"
#include "Core/Project/Project.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Conqueror::Editor::ObjectPicker
{
    namespace
    {
        struct State
        {
            bool Open = false;
            bool JustOpened = false;
            ImGuiID Owner = 0;
            std::string TypeName;
            std::vector<std::string> Extensions;
            std::string Selected;
            int Tab = 0;   // 0 Assets, 1 Packages, 2 Scene
            char Filter[96] = {};
            float Size = 72.0f;
            std::vector<std::string> Lists[3];
            // Sonuç: sahibi bir sonraki karede alır
            bool HasResult = false;
            std::string Result;
            bool CloseRequested = false;   // çift tıklama: fare bırakılınca kapanır (tık alttaki Viewport'a geçmesin)
        };
        State s;
        std::function<std::vector<std::string>()> s_SceneProvider;
        // GL dokuları: bilerek yok edilmez (statik yıkıcı GL bağlamı kapandıktan sonra çalışır, çıkışta çökerdi)
        auto& s_Thumbs = *new std::unordered_map<std::string, std::shared_ptr<Texture2D>>();

        std::string Lower(std::string v)
        {
            std::transform(v.begin(), v.end(), v.begin(), ::tolower);
            return v;
        }

        bool Matches(const std::string& path)
        {
            const std::string ext = Lower(fs::path(path).extension().string());
            return std::find(s.Extensions.begin(), s.Extensions.end(), ext) != s.Extensions.end();
        }

        std::vector<std::string> Scan(const fs::path& root)
        {
            std::vector<std::string> out;
            std::error_code ec;
            if (root.empty() || !fs::is_directory(root, ec))
                return out;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                    break;
                if (it->is_regular_file(ec) && Matches(it->path().string()))
                    out.push_back(it->path().lexically_normal().string());
            }
            std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
                return Lower(fs::path(a).stem().string()) < Lower(fs::path(b).stem().string());
            });
            return out;
        }

        void Refresh()
        {
            s.Lists[0] = Project::GetActive() ? Scan(Project::GetAssetDirectory()) : std::vector<std::string>{};
            s.Lists[1] = Scan(fs::path(SceneSerializer::ResolveAssetPath("CQ:Resources/Packages")));
            s.Lists[2].clear();
            if (s_SceneProvider)
                for (auto& p : s_SceneProvider())
                    if (!p.empty() && Matches(p) && std::find(s.Lists[2].begin(), s.Lists[2].end(), p) == s.Lists[2].end())
                        s.Lists[2].push_back(p);
        }

        bool IsImage(const std::string& path)
        {
            const std::string ext = Lower(fs::path(path).extension().string());
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
        }

        std::shared_ptr<Texture2D> Thumb(const std::string& path)
        {
            auto it = s_Thumbs.find(path);
            if (it != s_Thumbs.end())
                return it->second;
            auto tex = IsImage(path) ? Texture2D::Create(path) : nullptr;
            s_Thumbs[path] = tex;
            return tex;
        }

        void Pick(const std::string& path, bool close)
        {
            s.Selected = path;
            s.HasResult = true;
            s.Result = path;
            if (close)
                s.CloseRequested = true;
        }

        // Tek öğe: küçük resim (görsel) ya da tür simgesi + ad
        void Item(const std::string& path, const std::string& name)
        {
            const float size = s.Size;
            const ImVec2 cell(size + 16.0f, size + ImGui::GetTextLineHeightWithSpacing() * 2.0f);
            ImGui::PushID(path.c_str());
            ImGui::BeginGroup();
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const bool selected = s.Selected == path;
            if (ImGui::InvisibleButton("##cell", cell))
                Pick(path, false);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Pick(path, true);
            if (ImGui::IsItemHovered() && !path.empty())
                ImGui::SetTooltip("%s", path.c_str());
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (selected || ImGui::IsItemHovered())
                dl->AddRectFilled(p0, ImVec2(p0.x + cell.x, p0.y + cell.y), selected ? ImGui::GetColorU32(UI::Colors::AccentDim)
                                                                                        : IM_COL32(255, 255, 255, 18), 4.0f);
            const ImVec2 imgMin(p0.x + 8.0f, p0.y + 4.0f), imgMax(imgMin.x + size, imgMin.y + size);
            if (path.empty())
                dl->AddText(ImGui::GetFont(), size * 0.35f, ImVec2(imgMin.x + size * 0.3f, imgMin.y + size * 0.32f), IM_COL32(150, 150, 160, 255),
                            ICON_FA_BAN);
            else if (auto t = Thumb(path); t && t->GetRendererID())
            {
                // En-boy oranını koru
                float w = (float)t->GetWidth(), h = (float)t->GetHeight(), k = size / std::max(w, h);
                ImVec2 c((imgMin.x + imgMax.x) * 0.5f, (imgMin.y + imgMax.y) * 0.5f);
                dl->AddImage((ImTextureID)(uintptr_t)t->GetRendererID(), ImVec2(c.x - w * k * 0.5f, c.y - h * k * 0.5f),
                             ImVec2(c.x + w * k * 0.5f, c.y + h * k * 0.5f), ImVec2(0, 1), ImVec2(1, 0));
            }
            else
            {
                auto info = ContentBrowserPanel::GetAssetTypeInfo(path, false);
                dl->AddRectFilled(imgMin, imgMax, IM_COL32(48, 50, 56, 255), 6.0f);
                dl->AddText(ImGui::GetFont(), size * 0.42f, ImVec2(imgMin.x + size * 0.29f, imgMin.y + size * 0.28f), info.Color, info.Icon);
            }
            // Ad (kırpılmış)
            std::string label = name;
            const float maxW = cell.x - 4.0f;
            while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > maxW)
                label = label.substr(0, label.size() - 4) + "...";
            const float tw = ImGui::CalcTextSize(label.c_str()).x;
            dl->AddText(ImVec2(p0.x + (cell.x - tw) * 0.5f, imgMax.y + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }

    const std::vector<std::string>& ImageExtensions()
    {
        static const std::vector<std::string> exts = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".cqrt" };
        return exts;
    }

    void SetSceneProvider(std::function<std::vector<std::string>()> provider) { s_SceneProvider = std::move(provider); }

    void Open(ImGuiID owner, const std::string& typeName, const std::vector<std::string>& extensions, const std::string& current)
    {
        s.Open = true;
        s.JustOpened = true;
        s.Owner = owner;
        s.TypeName = typeName;
        s.Extensions.clear();
        for (auto& e : extensions)
            s.Extensions.push_back(Lower(e));
        s.Selected = current.empty() ? std::string() : fs::path(current).lexically_normal().string();
        s.Filter[0] = 0;
        s.HasResult = false;
        s.CloseRequested = false;
        Refresh();
        // Geçerli seçim paketteyse Packages sekmesiyle aç
        s.Tab = 0;
        if (!s.Selected.empty() && std::find(s.Lists[1].begin(), s.Lists[1].end(), s.Selected) != s.Lists[1].end())
            s.Tab = 1;
    }

    bool IsOpenFor(ImGuiID owner) { return s.Open && s.Owner == owner; }

    bool Consume(ImGuiID owner, std::string& picked)
    {
        if (!s.HasResult || s.Owner != owner)
            return false;
        s.HasResult = false;
        picked = s.Result;
        return true;
    }

    void Draw()
    {
        if (!s.Open)
            return;
        if (s.CloseRequested && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            s.CloseRequested = false;
            s.Open = false;
            return;
        }
        if (s.JustOpened)
        {
            ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowFocus();
            s.JustOpened = false;
        }
        const std::string title = "Select " + s.TypeName + "###ObjectPicker";
        bool open = true;
        if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
        {
            ImGui::End();
            if (!open)
                s.Open = false;
            return;
        }
        // Arama
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##pickerfilter", ICON_FA_MAGNIFYING_GLASS " Search", s.Filter, sizeof(s.Filter));

        // Sekmeler + boyut
        const char* tabs[] = { "Assets", "Packages", "Scene" };
        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
                ImGui::SameLine(0, 2);
            const bool active = s.Tab == i;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
            if (ImGui::Button(tabs[i]))
                s.Tab = i;
            if (active)
                ImGui::PopStyleColor();
        }
        const std::string filter = Lower(s.Filter);
        auto& list = s.Lists[s.Tab];
        int shown = 0;
        for (auto& p : list)
            if (filter.empty() || Lower(fs::path(p).stem().string()).find(filter) != std::string::npos)
                ++shown;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 170.0f + ImGui::GetCursorPosX());
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("##pickersize", &s.Size, 40.0f, 160.0f, "");
        ImGui::SameLine();
        ImGui::TextDisabled("%d", shown);

        // Izgara
        const float footer = ImGui::GetTextLineHeightWithSpacing() * 2.4f;
        ImGui::BeginChild("##pickergrid", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        const float cellW = s.Size + 16.0f + ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
        int col = 0;
        auto next = [&]() {
            if (++col < columns)
                ImGui::SameLine();
            else
                col = 0;
        };
        Item("", "None");
        next();
        for (auto& p : list)
        {
            const std::string name = fs::path(p).stem().string();
            if (!filter.empty() && Lower(name).find(filter) == std::string::npos)
                continue;
            Item(p, name);
            next();
        }
        if (list.empty())
        {
            ImGui::NewLine();
            ImGui::TextDisabled(s.Tab == 2 ? "No %s used in the open scene" : "No %s files here", s.TypeName.c_str());
        }
        ImGui::EndChild();

        // Alt bilgi: seçili öğe
        if (s.Selected.empty())
            ImGui::TextUnformatted("None");
        else
        {
            ImGui::TextUnformatted(fs::path(s.Selected).stem().string().c_str());
            ImGui::TextDisabled("%s", s.Selected.c_str());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            open = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            open = false;
        ImGui::End();
        if (!open)
            s.Open = false;
    }
}
