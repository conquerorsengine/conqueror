#include "AssetInspector.h"
#include "ContentBrowserPanel.h"
#include "NodeAssetPanel.h"
#include "Assets/SimpleAssetInspector.h"
#include "Assets/PlayableAssetInspector.h"
#include "Assets/CsvGridInspector.h"
#include "Assets/ParticleAssetInspector.h"
#include "Scripting/TestRunner.h"
#include "InspectorPanel.h"
#include "Assets/SceneTemplate.h"
#include "Assets/AssetUI.h"
#include "Assets/TileAssetInspector.h"
#include "Assets/SpriteAtlasInspector.h"
#include "Assets/VolumeProfileInspector.h"
#include "Assets/LightingAssetsInspector.h"
#include "Assets/LensFlareInspector.h"
#include "Assets/CustomRenderTextureInspector.h"
#include "Renderer/ComputeShaders.h"
#include "Renderer/RayTracingShaders.h"
#include "Assets/ShaderVariantInspector.h"
#include "Assets/AudioAssetsInspector.h"
#include "Assets/TerrainAssetsInspector.h"
#include "Assets/SignalReceiverInspector.h"
#include "Assets/InputActionsInspector.h"
#include "Assets/DataAssetInspector.h"
#include "Assets/ModelInspector.h"
#include "Assets/CurveInspector.h"
#include "Assets/CubemapInspector.h"
#include "Assets/AvatarMaskInspector.h"
#include "Assets/AnimatorOverrideInspector.h"
#include "Assets/BlendSpaceInspector.h"
#include "Assets/AnimationClipInspector.h"
#include "EditorState.h"
#include <yaml-cpp/yaml.h>
#include "Core/PhysicsSystem/PhysicsMaterial.h"
#include "Renderer/RenderTargetRegistry.h"
#include "MaterialEditorUI.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RHI/TextureSettings.h"
#include "Core/Logging/Log.h"
#include "Core/Animation/AnimationController.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace Conqueror::Editor::AssetInspector
{
    static std::pair<std::string, PhysicsMaterialAsset> s_PendingPhys;
    namespace
    {
        namespace fs = std::filesystem;

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        std::string FormatBytes(uint64_t bytes)
        {
            char buf[64];
            if (bytes >= 1024ull * 1024ull)
                std::snprintf(buf, sizeof(buf), "%.2f MB", (double)bytes / (1024.0 * 1024.0));
            else if (bytes >= 1024ull)
                std::snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
            else
                std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
            return buf;
        }

        // Düzenlenen doku durumu (seçim değişince sıfırlanır)
        struct TextureState
        {
            std::string Path;
            TextureImportSettings Original;
            TextureImportSettings Edited;
            bool HasMeta = false;
            // Önizleme (CPU'da ≤256 px; kanal maskesi ve mip uygulanmış)
            std::vector<uint8_t> Pixels;   // RGBA8, alt-sol köken
            int PixelW = 0, PixelH = 0;
            std::shared_ptr<Texture2D> Preview;
            bool ShowR = true, ShowG = true, ShowB = true, ShowA = true;
            int Mip = 0;
            bool PreviewDirty = true;
        };

        TextureState& State()
        {
            static TextureState s;
            return s;
        }

        void LoadPreviewPixels(TextureState& st)
        {
            st.Pixels.clear();
            st.PixelW = st.PixelH = 0;
            stbi_set_flip_vertically_on_load(1);
            int w = 0, h = 0, c = 0;
            stbi_uc* data = stbi_load(st.Path.c_str(), &w, &h, &c, 4);
            if (!data)
                return;
            const int maxPreview = 256;
            int tw = w, th = h;
            if (std::max(w, h) > maxPreview)
            {
                float k = (float)maxPreview / (float)std::max(w, h);
                tw = std::max(1, (int)std::lround(w * k));
                th = std::max(1, (int)std::lround(h * k));
            }
            st.Pixels.resize((size_t)tw * th * 4);
            if (tw != w || th != h)
                stbir_resize_uint8_srgb(data, w, h, 0, st.Pixels.data(), tw, th, 0, STBIR_RGBA);
            else
                std::memcpy(st.Pixels.data(), data, st.Pixels.size());
            stbi_image_free(data);
            st.PixelW = tw;
            st.PixelH = th;
        }

        void RebuildPreview(TextureState& st)
        {
            st.PreviewDirty = false;
            if (st.Pixels.empty())
                return;
            // Mip: önizlemeyi 2^mip küçült (en az 1 px)
            int w = st.PixelW, h = st.PixelH;
            std::vector<uint8_t> src = st.Pixels;
            for (int m = 0; m < st.Mip && (w > 1 || h > 1); ++m)
            {
                int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
                std::vector<uint8_t> dst((size_t)nw * nh * 4);
                stbir_resize_uint8_srgb(src.data(), w, h, 0, dst.data(), nw, nh, 0, STBIR_RGBA);
                src.swap(dst);
                w = nw;
                h = nh;
            }
            // Görüntü alanı için tekrar büyüt (mip'in bulanıklığı görünsün)
            std::vector<uint8_t> out((size_t)st.PixelW * st.PixelH * 4);
            if (w != st.PixelW || h != st.PixelH)
                stbir_resize_uint8_srgb(src.data(), w, h, 0, out.data(), st.PixelW, st.PixelH, 0, STBIR_RGBA);
            else
                out = src;
            // Kanal maskesi: tek kanal -> gri; alfa kapalıysa opak; checker zemin üzerine bileştir
            const int onlyChannels = (int)st.ShowR + (int)st.ShowG + (int)st.ShowB;
            for (int y = 0; y < st.PixelH; ++y)
                for (int x = 0; x < st.PixelW; ++x)
                {
                    uint8_t* p = &out[((size_t)y * st.PixelW + x) * 4];
                    uint8_t r = p[0], g = p[1], b = p[2], a = p[3];
                    if (onlyChannels == 1)
                    {
                        uint8_t v = st.ShowR ? r : st.ShowG ? g : b;
                        r = g = b = v;
                    }
                    else if (onlyChannels == 0 && st.ShowA)
                    {
                        r = g = b = a;
                        a = 255;
                    }
                    else
                    {
                        if (!st.ShowR) r = 0;
                        if (!st.ShowG) g = 0;
                        if (!st.ShowB) b = 0;
                    }
                    if (!st.ShowA || onlyChannels == 1)
                        a = 255;
                    // Alfa için dama tahtası zemin
                    const uint8_t checker = (((x / 8) + (y / 8)) & 1) ? 200 : 150;
                    const float t = a / 255.0f;
                    p[0] = (uint8_t)std::lround(r * t + checker * (1.0f - t));
                    p[1] = (uint8_t)std::lround(g * t + checker * (1.0f - t));
                    p[2] = (uint8_t)std::lround(b * t + checker * (1.0f - t));
                    p[3] = 255;
                }
            if (!st.Preview || st.Preview->GetWidth() != (uint32_t)st.PixelW || st.Preview->GetHeight() != (uint32_t)st.PixelH)
                st.Preview = Texture2D::Create((uint32_t)st.PixelW, (uint32_t)st.PixelH);
            if (st.Preview)
                st.Preview->SetData(out.data(), (uint32_t)out.size());
        }

        void SelectTexture(TextureState& st, const std::string& path)
        {
            st = TextureState{};
            st.Path = path;
            st.HasMeta = TextureImportSettings::Load(path, st.Original);
            st.Edited = st.Original;
            LoadPreviewPixels(st);
        }

        void DrawFileInfo(const std::string& path)
        {
            std::error_code ec;
            fs::path p(path);
            uint64_t size = fs::is_regular_file(p, ec) ? (uint64_t)fs::file_size(p, ec) : 0;
            UI::Text("File", "%s", p.filename().string().c_str());
            UI::Text("Size", "%s", FormatBytes(size).c_str());
            auto time = fs::last_write_time(p, ec);
            if (!ec)
            {
                auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                std::time_t tt = std::chrono::system_clock::to_time_t(sys);
                char buf[64];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&tt));
                UI::Text("Modified", "%s", buf);
            }
            ImGui::TextDisabled("%s", path.c_str());
        }
    }

    bool IsTexture(const std::string& path)
    {
        std::string e = Lower(fs::path(path).extension().string());
        return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || e == ".hdr" || e == ".psd" || e == ".gif";
    }

    void Invalidate()
    {
        State() = TextureState{};
    }

    bool ApplyTextureSettings(const std::string& path)
    {
        auto& st = State();
        if (st.Path != path)
            SelectTexture(st, path);
        if (!st.Edited.Save(path))
        {
            CQ_CORE_ERROR("[Texture] Cannot write {0}", TextureImportSettings::MetaPath(path));
            return false;
        }
        st.Original = st.Edited;
        st.HasMeta = true;
        Texture2D::ReloadFromDisk(path);
        return true;
    }

    static void DrawTexture(const std::string& path)
    {
        auto& st = State();
        if (st.Path != path)
            SelectTexture(st, path);
        if (st.PreviewDirty)
            RebuildPreview(st);

        // Başlık
        ImGui::TextColored(ImVec4(0.32f, 0.78f, 0.78f, 1.0f), ICON_FA_IMAGE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Texture Import Settings");
        ImGui::Separator();

        // Packages varlıkları değiştirilemez (Unity: içe aktarma ayarları kilitli)
        const bool readOnly = ContentBrowserPanel::IsReadOnly(path);
        if (readOnly)
            UI::InfoBox("This asset is in Packages and cannot be edited.", UI::InfoType::Info);
        ImGui::BeginDisabled(readOnly);

        // ---- Ayarlar
        auto& s = st.Edited;
        const char* types[] = { "Default", "Normal Map", "Sprite (2D and UI)", "UI" };
        int type = (int)s.Type;
        if (UI::Combo("Texture Type", type, types, 4, "Sprite / UI: no mipmaps and clamped edges by default"))
        {
            TextureImportSettings d = TextureImportSettings::DefaultsFor((TextureType)type);
            d.MaxSize = s.MaxSize;
            s = d;
        }
        if (s.Type == TextureType::Sprite)
            UI::DragFloat("Pixels Per Unit", s.PixelsPerUnit, 1.0f, 1.0f, 10000.0f, "%.0f", "Default for Sprite Renderers using this texture");
        if (s.Type == TextureType::NormalMap)
        {
            UI::Checkbox("Create From Grayscale", s.NormalFromGrayscale, "Treat the image as a height map and generate a normal map from it");
            if (s.NormalFromGrayscale)
                UI::SliderFloat("Bumpiness", s.Bumpiness, 0.1f, 20.0f, "%.2f");
        }
        UI::SubHeader("Sampling");
        const char* filters[] = { "Point (no filter)", "Bilinear", "Trilinear" };
        int filter = (int)s.Filter;
        if (UI::Combo("Filter Mode", filter, filters, 3, "Point keeps pixel art crisp"))
            s.Filter = (TextureFilter)filter;
        const char* wraps[] = { "Repeat", "Clamp", "Mirror" };
        int wrap = (int)s.Wrap;
        if (UI::Combo("Wrap Mode", wrap, wraps, 3))
            s.Wrap = (TextureWrap)wrap;
        UI::Checkbox("Generate Mipmaps", s.Mipmaps, "Smaller copies used at a distance: removes shimmering on 3D surfaces");
        ImGui::BeginDisabled(!s.Mipmaps || s.Filter == TextureFilter::Point);
        UI::SliderInt("Aniso Level", s.Anisotropy, 1, 16, "Sharper textures on surfaces seen at grazing angles");
        ImGui::EndDisabled();
        const char* sizes[] = { "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384" };
        int sizeIndex = 0;
        for (int i = 0; i < 10; ++i)
            if (std::atoi(sizes[i]) == s.MaxSize) sizeIndex = i;
        if (UI::Combo("Max Size", sizeIndex, sizes, 10, "Larger images are downscaled on import"))
            s.MaxSize = std::atoi(sizes[sizeIndex]);

        ImGui::Spacing();
        const bool dirty = s != st.Original;
        ImGui::BeginDisabled(s == st.Original);
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Revert", ImVec2(110, 0)))
            s = st.Original;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (UI::AccentButton(ICON_FA_CHECK "  Apply", ImVec2(110, 0)))
            ApplyTextureSettings(path);
        ImGui::EndDisabled();
        if (s != st.Original)
        {
            ImGui::SameLine();
            ImGui::TextColored(UI::Colors::Warning, "Unapplied changes");
        }
        ImGui::EndDisabled();

        // ---- Önizleme
        UI::SubHeader("Preview");
        auto toggle = [](const char* label, bool& v, ImVec4 color) {
            ImGui::PushStyleColor(ImGuiCol_Text, v ? color : ImVec4(0.45f, 0.45f, 0.5f, 1.0f));
            bool clicked = ImGui::SmallButton(label);
            ImGui::PopStyleColor();
            if (clicked) v = !v;
            return clicked;
        };
        bool changed = false;
        changed |= toggle("R", st.ShowR, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::SameLine();
        changed |= toggle("G", st.ShowG, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        ImGui::SameLine();
        changed |= toggle("B", st.ShowB, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
        ImGui::SameLine();
        changed |= toggle("A", st.ShowA, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        auto live = Texture2D::Create(path);
        const int maxMip = live ? (int)live->GetMipLevels() - 1 : 0;
        if (ImGui::SliderInt("Mip", &st.Mip, 0, std::max(0, maxMip)))
            changed = true;
        if (changed)
            RebuildPreview(st);
        if (st.Preview && st.PixelW > 0)
        {
            float avail = ImGui::GetContentRegionAvail().x;
            float scale = std::min(avail / (float)st.PixelW, 256.0f / (float)st.PixelH);
            ImVec2 size((float)st.PixelW * scale, (float)st.PixelH * scale);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - size.x) * 0.5f));
            ImGui::Image((ImTextureID)(uint64_t)st.Preview->GetRendererID(), size, ImVec2(0, 1), ImVec2(1, 0));
        }

        // ---- Bilgi
        UI::SubHeader("Info");
        if (live)
        {
            UI::Text("Source", "%u x %u  (%u channel%s)", live->GetSourceWidth(), live->GetSourceHeight(), live->GetSourceChannels(),
                     live->GetSourceChannels() == 1 ? "" : "s");
            UI::Text("Imported", "%u x %u  %s", live->GetWidth(), live->GetHeight(), live->GetFormatName().c_str());
            UI::Text("Mipmaps", "%u level%s", live->GetMipLevels(), live->GetMipLevels() == 1 ? "" : "s");
            UI::Text("GPU Memory", "%s", FormatBytes(live->GetMemoryBytes()).c_str());
            const auto& active = live->GetImportSettings();
            UI::Text("Sampler", "%s, %s, aniso %d", TextureImportSettings::FilterName(active.Filter), TextureImportSettings::WrapName(active.Wrap),
                     active.Anisotropy);
        }
        DrawFileInfo(path);
    }

    // ---- Anim Montage (.cqmontage): klip, karışma, kemik filtresi, bölümler ve notify'lar
    namespace
    {
        struct MontageState
        {
            std::string Path;
            AnimMontage Edited;
            bool Loaded = false;
            bool Dirty = false;
        };

        MontageState& Montage()
        {
            static MontageState st;
            return st;
        }

        bool InputString(const char* label, std::string& value, const char* hint = nullptr)
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s", value.c_str());
            bool changed = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof(buf)) : ImGui::InputText(label, buf, sizeof(buf));
            if (changed)
                value = buf;
            return changed;
        }
    }

    static void DrawMontage(const std::string& path)
    {
        auto& st = Montage();
        if (st.Path != path)
        {
            st = MontageState{};
            st.Path = path;
            if (auto m = AnimMontage::Load(path))
            {
                st.Edited = *m;
                st.Loaded = true;
            }
        }
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_PERSON_RUNNING);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Anim Montage%s", st.Dirty ? "  (modified)" : "");
        ImGui::Separator();
        if (!st.Loaded)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Cannot read this montage file.");
            DrawFileInfo(path);
            return;
        }
        AnimMontage& m = st.Edited;
        bool changed = false;
        ImGui::PushItemWidth(-110.0f);
        changed |= InputString("Name", m.Name);
        changed |= InputString("Clip Name", m.ClipName, "clip in the model (e.g. Attack)");
        changed |= ImGui::InputInt("Clip Index", &m.ClipIndex);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Used when Clip Name is empty or not found");
        changed |= ImGui::DragFloat("Rate", &m.Rate, 0.01f, 0.05f, 10.0f, "x%.2f");
        changed |= ImGui::DragFloat("Blend In", &m.BlendIn, 0.005f, 0.0f, 5.0f, "%.2f s");
        changed |= ImGui::DragFloat("Blend Out", &m.BlendOut, 0.005f, 0.0f, 5.0f, "%.2f s");
        changed |= InputString("From Bone", m.FromBone, "empty = whole body");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only this bone and its children play the montage (Layered Blend per Bone)");
        ImGui::PopItemWidth();

        UI::SubHeader("Sections");
        int removeSection = -1;
        for (int i = 0; i < (int)m.Sections.size(); ++i)
        {
            auto& sec = m.Sections[(size_t)i];
            ImGui::PushID(i);
            const float w = (ImGui::GetContentRegionAvail().x - 28.0f - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
            ImGui::SetNextItemWidth(w);
            changed |= InputString("##sname", sec.Name, "Name");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w);
            changed |= ImGui::SliderFloat("##sstart", &sec.Start, 0.0f, 1.0f, "at %.3f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w);
            changed |= InputString("##snext", sec.Next, "next (Stop / name)");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Empty: continue into the following section. Stop: end the montage. A section name: jump there (itself = loop).");
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeSection = i;
            ImGui::PopID();
        }
        if (removeSection >= 0) { m.Sections.erase(m.Sections.begin() + removeSection); changed = true; }
        if (ImGui::Button("+ Add Section"))
        {
            m.Sections.push_back({ "Section" + std::to_string(m.Sections.size() + 1), m.Sections.empty() ? 0.0f : std::min(1.0f, m.Sections.back().Start + 0.25f), "" });
            changed = true;
        }

        UI::SubHeader("Notifies");
        int removeNotify = -1;
        for (int i = 0; i < (int)m.Notifies.size(); ++i)
        {
            auto& nt = m.Notifies[(size_t)i];
            ImGui::PushID(1000 + i);
            const float w = (ImGui::GetContentRegionAvail().x - 28.0f - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
            ImGui::SetNextItemWidth(w);
            changed |= InputString("##nname", nt.Name, "Name / Sound:path");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w);
            changed |= ImGui::SliderFloat("##ntime", &nt.Time, 0.0f, 1.0f, "at %.3f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w);
            changed |= ImGui::DragFloat("##ndur", &nt.Duration, 0.005f, 0.0f, 1.0f, nt.Duration > 0.0f ? "state %.3f" : "instant");
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeNotify = i;
            ImGui::PopID();
        }
        if (removeNotify >= 0) { m.Notifies.erase(m.Notifies.begin() + removeNotify); changed = true; }
        if (ImGui::Button("+ Add Notify"))
        {
            m.Notifies.push_back({ "Notify" + std::to_string(m.Notifies.size() + 1), 0.5f, 0.0f });
            changed = true;
        }
        if (changed)
            st.Dirty = true;

        ImGui::Spacing();
        ImGui::BeginDisabled(!st.Dirty);
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save", ImVec2(110, 0)))
        {
            AnimMontage sorted = m;
            std::stable_sort(sorted.Sections.begin(), sorted.Sections.end(), [](const AnimMontageSection& a, const AnimMontageSection& b) { return a.Start < b.Start; });
            if (sorted.Serialize(path))
            {
                m = sorted;
                st.Dirty = false;
            }
            else
                CQ_CORE_ERROR("[Montage] Cannot write {0}", path);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Revert", ImVec2(110, 0)))
            st.Path.clear();
        ImGui::EndDisabled();
        ImGui::TextDisabled("Play from scripts: AnimMontagePlay(null, \"%s\")", PrefabSerializer::ToProjectPath(fs::path(path)).c_str());
        ImGui::Separator();
        DrawFileInfo(path);
    }

    static void AssetHeader(const char* icon, const std::string& path, const char* type)
    {
        ImGui::TextColored(UI::Colors::Accent, "%s", icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", type);
        ImGui::Separator();
    }

    static void DrawPhysicsMaterial(const std::string& path)
    {
        AssetHeader(ICON_FA_BASKETBALL, path, PhysicsMaterials::IsFile2D(path) ? "Physics Material 2D" : "Physics Material");
        PhysicsMaterialAsset m;
        if (!PhysicsMaterials::Load(path, m))
        {
            UI::InfoBox("The physics material could not be read.", UI::InfoType::Error);
            return;
        }
        bool changed = false;
        changed |= UI::SliderFloat("Friction", m.Friction, 0.0f, 1.0f, "%.2f", "0 = ice, 1 = rubber");
        changed |= UI::SliderFloat("Bounciness", m.Bounciness, 0.0f, 1.0f, "%.2f", "0 = no bounce, 1 = bounces without losing energy");
        if (changed && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            PhysicsMaterials::Save(m, path);
        else if (changed)
            s_PendingPhys = { path, m };
        if (!s_PendingPhys.first.empty() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            PhysicsMaterials::Save(s_PendingPhys.second, s_PendingPhys.first);
            s_PendingPhys.first.clear();
        }
        UI::InfoBox("Drag onto a collider's Material slot. Bodies using it update when it changes.", UI::InfoType::Info);
    }

    static void DrawRenderTexture(const std::string& path)
    {
        AssetHeader(ICON_FA_TV, path, "Render Texture");
        std::string name;
        int w = 512, h = 512;
        if (!RenderTargetRegistry::ReadAsset(path, name, w, h))
        {
            UI::InfoBox("The render texture could not be read.", UI::InfoType::Error);
            return;
        }
        bool changed = false;
        changed |= UI::DragInt("Width", w, 1.0f, 1, 4096);
        changed |= UI::DragInt("Height", h, 1.0f, 1, 4096);
        if (changed)
        {
            RenderTargetRegistry::WriteAsset(path, w, h);
            if (auto rt = RenderTargetRegistry::Find(name))
                rt->Resize((uint32_t)w, (uint32_t)h);
        }
        if (auto rt = RenderTargetRegistry::Find(name); rt && rt->GetRendererID())
        {
            const float side = std::min(ImGui::GetContentRegionAvail().x, 256.0f);
            ImGui::Image((ImTextureID)(uintptr_t)rt->GetRendererID(), ImVec2(side, side * (float)h / (float)w), ImVec2(0, 1), ImVec2(1, 0));
        }
        UI::InfoBox("A camera renders into it via Render Target > Target Texture. Use it in any texture slot.", UI::InfoType::Info);
    }

    namespace
    {
        void DrawPrefab(const std::string& path)
        {
            const bool variant = PrefabSerializer::IsVariant(path);
            ImGui::TextColored(variant ? ImVec4(0.67f, 0.55f, 1.0f, 1.0f) : UI::Colors::Accent, ICON_FA_CUBES_STACKED);
            ImGui::SameLine();
            ImGui::TextUnformatted((fs::path(path).filename().string() + (variant ? "  (Prefab Variant)" : "  (Prefab)")).c_str());
            ImGui::Separator();
            if (!variant)
            {
                ImGui::TextWrapped("Drag it into the Scene view or the Hierarchy to place it. Right-click > Create Prefab Variant makes a "
                                   "prefab based on this one.");
                DrawFileInfo(path);
                return;
            }
            static std::string cachedPath;
            static int overrides = 0, added = 0, removed = 0;
            static fs::file_time_type cachedTime;
            std::error_code ec;
            const auto t = fs::last_write_time(path, ec);
            if (cachedPath != path || cachedTime != t)
            {
                cachedPath = path;
                cachedTime = t;
                overrides = added = removed = 0;
                try
                {
                    YAML::Node v = YAML::LoadFile(path)["PrefabVariant"];
                    if (v["Overrides"]) for (const auto& o : v["Overrides"]) overrides += (o["Components"] ? (int)o["Components"].size() : 0) + (o["Removed"] ? (int)o["Removed"].size() : 0);
                    if (v["Added"]) added = (int)v["Added"].size();
                    if (v["RemovedEntities"]) removed = (int)v["RemovedEntities"].size();
                }
                catch (...) {}
            }
            std::string base = PrefabSerializer::GetVariantBase(path);
            if (AssetUI::BeginProperties("##variant"))
            {
                AssetUI::Row("Base Prefab");
                std::string edited = base;
                if (AssetUI::AssetSlot("base", "Prefab", { ".cqprefab" }, edited) && !edited.empty() &&
                    fs::weakly_canonical(AssetUI::Resolve(edited), ec) != fs::weakly_canonical(path, ec))
                {
                    // Taban değişimi: farklar korunur, yeni tabana uygulanır
                    try
                    {
                        YAML::Node root = YAML::LoadFile(path);
                        root["PrefabVariant"]["Base"] = edited;
                        YAML::Emitter e;
                        e << root;
                        std::ofstream(path, std::ios::binary | std::ios::trunc) << e.c_str() << "\n";
                    }
                    catch (...) {}
                }
                AssetUI::Row("Overridden components");
                ImGui::Text("%d", overrides);
                AssetUI::Row("Added objects");
                ImGui::Text("%d", added);
                AssetUI::Row("Removed objects");
                ImGui::Text("%d", removed);
                AssetUI::EndProperties();
            }
            if (ImGui::Button(ICON_FA_ARROW_UP "  Select Base Prefab") && !base.empty())
                EditorState::Get().SelectAsset(AssetUI::Resolve(base).string());
            ImGui::Spacing();
            ImGui::TextWrapped("Place it in a scene, change it and press Apply on the instance: only the differences from the base are saved. "
                               "Changes to the base prefab show up in this variant unless the variant overrides them.");
        }
    }

    void Draw(const std::string& path)
    {
        if (Lower(fs::path(path).extension().string()) == PrefabSerializer::Extension)
        {
            DrawPrefab(path);
            return;
        }
        if (IsTexture(path))
        {
            DrawTexture(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqphysmat" || Lower(fs::path(path).extension().string()) == ".cqphysmat2d")
        {
            DrawPhysicsMaterial(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqrt")
        {
            DrawRenderTexture(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqmat" || Lower(fs::path(path).extension().string()) == ".cqmatfunc")
        {
            MaterialEditorUI::DrawAssetInspector(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqblendspace")
        {
            BlendSpaceInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqoverride")
        {
            AnimatorOverrideInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqmask")
        {
            AvatarMaskInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqanim")
        {
            AnimationClipInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqcubemap")
        {
            CubemapInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqcurve")
        {
            CurveInspector::Draw(path);
            return;
        }
        if (ModelInspector::IsModel(path))
        {
            ModelInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqs" && Scripting::TestRunner::IsEditorUtility(path))
        {
            ImGui::TextColored(UI::Colors::Accent, ICON_FA_WAND_MAGIC_SPARKLES);
            ImGui::SameLine();
            ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("Editor Utility");
            ImGui::Separator();
            static std::string s_UtilityError;
            if (UI::ButtonFullWidth(ICON_FA_PLAY "  Run", 28.0f) && InspectorPanel::s_SharedScene)
            {
                s_UtilityError.clear();
                Scripting::TestRunner::RunEditorUtility(path, InspectorPanel::s_SharedScene().get(), &s_UtilityError);
            }
            if (!s_UtilityError.empty())
                UI::InfoBox(s_UtilityError.c_str(), UI::InfoType::Error);
            ImGui::Spacing();
        }
        if (ParticleAssetInspector::IsParticleAsset(path))
        {
            ParticleAssetInspector::Draw(path);
            return;
        }
        if (CsvGridInspector::IsGrid(path))
        {
            CsvGridInspector::Draw(path);
            return;
        }
        if (PlayableAssetInspector::IsPlayable(path))
        {
            PlayableAssetInspector::Draw(path);
            return;
        }
        if (SimpleAssetInspector::IsSimpleAsset(path))
        {
            SimpleAssetInspector::Draw(path);
            return;
        }
        if (const NodeAssetSchema* graph = NodeAssetPanel::SchemaForExtension(fs::path(path).extension().string()))
        {
            ImGui::TextColored(UI::Colors::Accent, "%s", graph->Icon.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", graph->Title.c_str());
            ImGui::Separator();
            NodeAssets::File file;
            if (NodeAssets::Load(path, file))
                ImGui::Text("%d nodes, %d links", (int)file.Nodes.size(), (int)file.Links.size());
            ImGui::Spacing();
            if (ImGui::Button((std::string(ICON_FA_DIAGRAM_PROJECT "  Open ") + graph->Title).c_str()) && NodeAssetPanel::s_Open)
                NodeAssetPanel::s_Open(path);
            if (!graph->MenuTooltip.empty())
            {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", graph->MenuTooltip.c_str());
            }
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqdata")
        {
            DataAssetInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqinputactions")
        {
            InputActionsInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqsignal")
        {
            SignalReceiverInspector::DrawAsset(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqterrainlayer")
        {
            TerrainAssetsInspector::DrawLayer(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqbrush")
        {
            TerrainAssetsInspector::DrawBrush(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqaudiorandom")
        {
            AudioAssetsInspector::DrawRandomContainer(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqmixer")
        {
            AudioAssetsInspector::DrawMixer(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqraytrace")
        {
            ImGui::TextColored(UI::Colors::Accent, ICON_FA_WAND_MAGIC_SPARKLES);
            ImGui::SameLine();
            ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("Ray Tracing Shader");
            ImGui::Separator();
            const std::string error = RayTracingShaders::GetError(path);
            if (!error.empty())
                UI::InfoBox(error.c_str(), UI::InfoType::Error);
            else
                UI::InfoBox("Compiled.", UI::InfoType::Success);
            if (RayTracingShaders::GetTriangleCount() > 0)
                UI::Text("Last acceleration structure", "%d triangles, %d BVH nodes", RayTracingShaders::GetTriangleCount(), RayTracingShaders::GetNodeCount());
            ImGui::TextWrapped("Run it from a script: RayTracingDispatch(\"%s\", \"Assets/<render texture>.cqrt\"). The active scene's "
                               "Mesh Renderers form the acceleration structure; the primary camera generates the rays.",
                               AssetUI::ToProjectRelative(path).c_str());
            if (ImGui::Button(ICON_FA_FILE_CODE "  Edit") && InspectorPanel::s_OpenCodeEditor)
                InspectorPanel::s_OpenCodeEditor(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqshadervariants")
        {
            ShaderVariantInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".comp")
        {
            ImGui::TextColored(UI::Colors::Accent, ICON_FA_MICROCHIP);
            ImGui::SameLine();
            ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("Compute Shader");
            ImGui::Separator();
            const auto info = ComputeShaders::Inspect(path);
            if (!info.Compiled)
                UI::InfoBox(info.Error.empty() ? "Not compiled." : info.Error.c_str(), UI::InfoType::Error);
            else
            {
                UI::Text("Thread Group Size", "%d x %d x %d", info.GroupSize.x, info.GroupSize.y, info.GroupSize.z);
                UI::SubHeader("Parameters");
                for (const auto& [name, type] : info.Uniforms)
                    UI::Text(name.c_str(), "%s", type.c_str());
            }
            ImGui::Spacing();
            ImGui::TextWrapped("Run it from a script: ComputeSetTexture / ComputeSetFloat / ComputeSetInt / ComputeSetVector, "
                               "then ComputeDispatch(shader, groupsX, groupsY, groupsZ). image2D parameters write into Render Textures.");
            if (ImGui::Button(ICON_FA_FILE_CODE "  Edit") && InspectorPanel::s_OpenCodeEditor)
                InspectorPanel::s_OpenCodeEditor(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqcrt")
        {
            CustomRenderTextureInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqflare")
        {
            LensFlareInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqlighting")
        {
            LightingAssetsInspector::DrawLightingSettings(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqlmparams")
        {
            LightingAssetsInspector::DrawLightmapParameters(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqvolume")
        {
            VolumeProfileInspector::DrawAsset(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqspriteatlas")
        {
            SpriteAtlasInspector::Draw(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqtile")
        {
            TileAssetInspector::DrawTile(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqpalette")
        {
            TileAssetInspector::DrawPalette(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == SceneTemplates::Extension)
        {
            SceneTemplates::DrawInspector(path);
            return;
        }
        if (Lower(fs::path(path).extension().string()) == ".cqmontage")
        {
            DrawMontage(path);
            return;
        }
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_FILE);
        ImGui::SameLine();
        ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
        ImGui::Separator();
        DrawFileInfo(path);
    }
}
