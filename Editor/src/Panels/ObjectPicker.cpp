#include "ObjectPicker.h"
#include "NodeAssetPanel.h"
#include "Assets/AssetPreview.h"
#include "Core/Assets/SimpleAsset.h"

#include "ContentBrowserPanel.h"
#include "Core/Project/Project.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <imgui.h>
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
            int UnconsumedFrames = 0;   // kapandıktan sonra alınmayan seçim (yuva artık çizilmiyor) birkaç karede düşer
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
            if (s.Extensions.empty())
                return fs::path(path).filename().string()[0] != '.';   // her dosya (gizliler hariç)
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
            if (AssetPreview::CanPreview(path))
                return AssetPreview::Get(path);
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
            // Görünmez düğmenin etiketi ad (otomasyon / ekran okuyucu öğeyi adıyla bulur), çizim aşağıda
            if (ImGui::InvisibleButton((name + "##cell").c_str(), cell))
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
        static const std::vector<std::string> exts = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".cqrt", ".cqcrt" };
        return exts;
    }

    const std::vector<std::string>& ModelExtensions()
    {
        static const std::vector<std::string> exts = { ".glb", ".gltf", ".fbx", ".obj", ".dae", ".blend", ".3ds" };
        return exts;
    }

    const std::vector<std::string>& AudioExtensions()
    {
        static const std::vector<std::string> exts = { ".wav", ".mp3", ".ogg", ".flac", ".cqaudiorandom", ".cqsoundcue" };
        return exts;
    }

    const std::vector<std::string>& FontExtensions()
    {
        static const std::vector<std::string> exts = { ".ttf", ".otf", ".cqfontasset", ".cqcustomfont" };
        return exts;
    }

    std::vector<std::string> ParseFilter(const std::string& filter)
    {
        std::vector<std::string> out;
        std::string cur;
        auto flush = [&]() {
            std::string e = cur;
            cur.clear();
            while (!e.empty() && (e.front() == ' ' || e.front() == '*'))
                e.erase(e.begin());
            while (!e.empty() && e.back() == ' ')
                e.pop_back();
            if (e.empty())
                return;
            if (e[0] != '.')
                e = "." + e;
            out.push_back(Lower(e));
        };
        for (char c : filter)
        {
            if (c == ';' || c == ',' || c == '|')
                flush();
            else
                cur += c;
        }
        flush();
        return out;
    }

    std::string TypeNameFor(const std::vector<std::string>& exts)
    {
        if (exts.empty())
            return "Asset";
        auto has = [&](const char* e) { return std::find(exts.begin(), exts.end(), e) != exts.end(); };
        if (exts.size() == 1)
            if (const SimpleAssets::Def* simple = SimpleAssets::ForExtension(exts.front()))
                return simple->Label;
        if (exts.size() == 1)
            if (const NodeAssetSchema* graph = NodeAssetPanel::SchemaForExtension(exts.front()))
                return graph->Title;
        if (has(".cqplayable")) return "Playable Asset";
        if (has(".cqmat")) return "Material";
        if (has(".cqphysmat2d")) return "Physics Material 2D";
        if (has(".cqtile")) return "Tile";
        if (has(".cqspriteatlas")) return "Sprite Atlas";
        if (has(".cqvolume")) return "Volume Profile";
        if (has(".cqflare")) return "Lens Flare";
        if (has(".cqcrt")) return "Custom Render Texture";
        if (has(".cqmixer")) return "Audio Mixer";
        if (has(".cqaudiorandom")) return "Audio Random Container";
        if (has(".cqterrainlayer")) return "Terrain Layer";
        if (has(".cqbrush")) return "Brush";
        if (has(".cqsignal")) return "Signal";
        if (has(".cqinputactions")) return "Input Actions";
        if (has(".cqdata")) return "Data Asset";
        if (has(".cqcurve")) return "Curve";
        if (has(".cqmask")) return "Avatar Mask";
        if (has(".cqcubemap") && !has(".hdr")) return "Cubemap";
        if (has(".cqlighting")) return "Lighting Settings";
        if (has(".cqlmparams")) return "Lightmap Parameters";
        if (has(".cqpalette")) return "Tile Palette";
        if (has(".cqphysmat")) return "Physics Material";
        if (has(".cqmesh")) return "Mesh";
        if (has(".cqblendspace")) return "Blend Space";
        if (has(".cqoverride")) return "Animator Override Controller";
        if (has(".cqrt") && exts.size() == 1) return "Render Texture";
        if (has(".png") || has(".jpg")) return "Texture";
        if (has(".wav") || has(".ogg") || has(".mp3")) return "Audio Clip";
        if (has(".glb") || has(".fbx") || has(".obj") || has(".gltf")) return "Model";
        if (has(".ttf") || has(".otf")) return "Font";
        if (has(".cqprefab")) return "Prefab";
        if (has(".cqscene")) return "Scene";
        if (has(".cqtimeline")) return "Timeline";
        if (has(".cqcont")) return "Animator Controller";
        if (has(".cqmontage")) return "Anim Montage";
        if (has(".cqgraph")) return "Visual Script";
        if (has(".mp4") || has(".webm")) return "Video Clip";
        return "Asset";
    }

    bool IconButton(const char* label)
    {
        const float h = ImGui::GetFrameHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::Button(label, ImVec2(h, h));
        // Simge görünen (mürekkep) sınırlarına göre piksel hassasında ortalanır: metin hizalaması glyph'in ilerleme
        // genişliğini kullandığından daire-nokta simgesi kutunun ortasından kayık duruyordu
        if (ImFontGlyph* glyph = ImGui::GetFontBaked()->FindGlyphNoFallback((ImWchar)0xf140))
        {
            const float x = std::round(pos.x + h * 0.5f - (glyph->X0 + glyph->X1) * 0.5f);
            const float y = std::round(pos.y + h * 0.5f - (glyph->Y0 + glyph->Y1) * 0.5f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), ICON_FA_BULLSEYE);
        }
        return pressed;
    }

    bool Button(const char* id, const std::string& typeName, const std::vector<std::string>& extensions, const std::string& current,
                std::string& picked)
    {
        const ImGuiID owner = ImGui::GetID(id);
        if (IconButton((std::string("##") + id).c_str()))
            Open(owner, typeName, extensions, current);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select %s", typeName.c_str());
        return Consume(owner, picked);
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

    bool IsBusy() { return s.Open || s.HasResult; }

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
        {
            if (s.HasResult && ++s.UnconsumedFrames > 3)
                s.HasResult = false;
            return;
        }
        s.UnconsumedFrames = 0;
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
