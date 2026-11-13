#include "AssetInspector.h"
#include "VersionControl/VersionControl.h"
#include "Build/AssetBundleBuilder.h"
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
#include "Renderer/SpriteSheet.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/CubemapAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Animation/AnimationController.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
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
        ApplyImportSideEffects(path, st.Edited);
        Texture2D::ReloadFromDisk(path);
        return true;
    }

    void ApplyImportSideEffects(const std::string& path, const TextureImportSettings& s)
    {
        std::error_code ec;
        // Texture Shape: Cube -> görselden cubemap varlığı
        if (s.Shape == TextureShape::Cube && s.Type != TextureType::Sprite && s.Type != TextureType::UI)
        {
            const fs::path cube = fs::path(path).replace_extension(CubemapAssets::Extension);
            if (!fs::exists(cube, ec))
            {
                CubemapAssetData data;
                data.Source = CubemapAssetData::SourceKind::Panorama;
                data.Panorama = SceneSerializer::ToAssetPath(path);
                CubemapAssets::Save(cube.string(), data);
            }
        }
        if (s.Type != TextureType::Sprite)
            return;
        SpriteSheet sheet;
        sheet.TexturePath = path;
        sheet.Load();
        stbi_set_flip_vertically_on_load(0);
        int w = 0, h = 0, channels = 0;
        unsigned char* rgba = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!rgba)
            return;
        const glm::vec4 fullRect(0.0f, 0.0f, (float)w, (float)h);
        // Single / Polygon: tüm görsel tek sprite (dilimli eski veri silinmez: Multiple'a dönülürse yerinde)
        if (s.SpriteMode != SpriteImportMode::Multiple && sheet.Sprites.empty())
        {
            SpriteDef d;
            d.Name = fs::path(path).stem().string();
            d.Rect = fullRect;
            sheet.Sprites.push_back(d);
        }
        if (s.SpriteMode == SpriteImportMode::Polygon && sheet.Sprites.size() == 1)
        {
            // Düzgün çokgen: dikdörtgene oturan elips üzerinde (ilk köşe üstte)
            SpriteDef& d = sheet.Sprites[0];
            std::vector<glm::vec2> polygon;
            const glm::vec2 c(d.Rect.z * 0.5f, d.Rect.w * 0.5f);
            for (int i = 0; i < std::max(s.PolygonSides, 3); ++i)
            {
                const float a = glm::half_pi<float>() + glm::two_pi<float>() * (float)i / (float)std::max(s.PolygonSides, 3);
                polygon.emplace_back(c.x + std::cos(a) * c.x, c.y + std::sin(a) * c.y);
            }
            d.Outlines = { polygon };
        }
        // Mesh Type: Tight -> Custom Outline'ı olmayan dilimlere görselden dış çizgi
        if (s.TightMesh)
            for (auto& d : sheet.Sprites)
                if (d.Outlines.empty())
                    d.Outlines = SpriteSheet::GenerateOutline(rgba, w, h, d.Rect, 0.5f);
        stbi_image_free(rgba);
        sheet.FullRectMesh = !s.TightMesh;
        sheet.ExtrudeEdges = s.TightMesh ? s.ExtrudeEdges : 0;
        if (!sheet.Sprites.empty())
            sheet.Save();
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

        // İçerik Tarayıcısı > okundan seçilen dilim: adı, boyutu ve önizlemesi
        if (const std::string& sliceName = EditorState::Get().GetSelectedAssetSlice(); !sliceName.empty())
            if (auto sheet = SpriteSheet::Get(path))
                if (const SpriteDef* slice = sheet->Find(sliceName))
                {
                    if (UI::BeginSection("Sprite", true, ICON_FA_OBJECT_UNGROUP))
                    {
                        UI::Text("Name", "%s", slice->Name.c_str());
                        UI::Text("Rect", "%d, %d  -  %d x %d", (int)slice->Rect.x, (int)slice->Rect.y, (int)slice->Rect.z, (int)slice->Rect.w);
                        UI::Text("Pivot", "%.2f, %.2f", slice->Pivot.x, slice->Pivot.y);
                        // Doku çizim bitene kadar tutulur (doku önbelleği zayıf başvuru tutar)
                        static std::shared_ptr<Texture2D> s_SliceTexture;
                        if (!s_SliceTexture || s_SliceTexture->GetPath() != path)
                            s_SliceTexture = Texture2D::Create(path);
                        if (auto texture = s_SliceTexture; texture && texture->GetWidth() > 0 && texture->GetHeight() > 0 && slice->Rect.z > 0.0f && slice->Rect.w > 0.0f)
                        {
                            const float W = (float)texture->GetWidth(), H = (float)texture->GetHeight();
                            const float maxW = ImGui::GetContentRegionAvail().x, maxH = 140.0f;
                            const float scale = std::min(maxW / slice->Rect.z, maxH / slice->Rect.w);
                            const ImVec2 size(slice->Rect.z * scale, slice->Rect.w * scale);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (maxW - size.x) * 0.5f);
                            ImGui::Image((ImTextureID)(uint64_t)texture->GetRendererID(), size, ImVec2(slice->Rect.x / W, (slice->Rect.y + slice->Rect.w) / H),
                                         ImVec2((slice->Rect.x + slice->Rect.z) / W, slice->Rect.y / H));
                        }
                        UI::EndSection();
                    }
                    ImGui::Separator();
                }

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
            d.Resize = s.Resize;
            d.Format = s.Format;
            d.Compression = s.Compression;
            d.UseCrunch = s.UseCrunch;
            d.CrunchQuality = s.CrunchQuality;
            for (int i = 0; i < kTexturePlatformCount; ++i)
                d.Platforms[i] = s.Platforms[i];
            s = d;
        }
        const bool spriteLike = s.Type == TextureType::Sprite || s.Type == TextureType::UI;
        {
            const char* shapes[] = { "2D", "Cube" };
            int shape = spriteLike ? 0 : (int)s.Shape;
            ImGui::BeginDisabled(spriteLike);
            if (UI::Combo("Texture Shape", shape, shapes, 2, "Cube: Apply creates a cubemap asset (.cqcubemap) from this panorama image"))
                s.Shape = (TextureShape)shape;
            ImGui::EndDisabled();
        }
        if (s.Type == TextureType::Sprite)
        {
            const char* spriteModes[] = { "Single", "Multiple", "Polygon" };
            int spriteMode = (int)s.SpriteMode;
            if (UI::Combo("Sprite Mode", spriteMode, spriteModes, 3, "Single: the whole image is one sprite. Multiple: several slices. Polygon: a regular polygon"))
                s.SpriteMode = (SpriteImportMode)spriteMode;
            ImGui::Indent(14.0f);
            if (s.SpriteMode == SpriteImportMode::Polygon)
                UI::SliderInt("Polygon Sides", s.PolygonSides, 3, 64, "Number of sides of the polygon sprite");
            UI::DragFloat("Pixels Per Unit", s.PixelsPerUnit, 1.0f, 1.0f, 10000.0f, "%.0f", "Default for Sprite Renderers using this texture");
            const char* meshTypes[] = { "Full Rect", "Tight" };
            int meshType = s.TightMesh ? 1 : 0;
            if (UI::Combo("Mesh Type", meshType, meshTypes, 2, "Tight: the sprite is drawn only where it has pixels (Apply generates the outline). Full Rect: a quad"))
                s.TightMesh = meshType == 1;
            ImGui::BeginDisabled(!s.TightMesh);
            UI::SliderInt("Extrude Edges", s.ExtrudeEdges, 0, 32, "Pixels added around the Tight mesh so filtering does not cut the edge");
            ImGui::EndDisabled();
            UI::Checkbox("Generate Physics Shape", s.GeneratePhysicsShape, "Polygon Collider 2D follows the sprite's shape; off: a rectangle");
            ImGui::Unindent(14.0f);
            // Salt okunur görselde de açılır (görüntüleme)
            ImGui::EndDisabled();
            if (UI::ButtonFullWidth(ICON_FA_OBJECT_UNGROUP "  Open Sprite Editor") && InspectorPanel::s_OpenSpriteEditor)
                InspectorPanel::s_OpenSpriteEditor(path);
            ImGui::BeginDisabled(readOnly);
        }
        if (s.Type == TextureType::NormalMap)
        {
            UI::Checkbox("Create From Grayscale", s.NormalFromGrayscale, "Treat the image as a height map and generate a normal map from it");
            if (s.NormalFromGrayscale)
                UI::SliderFloat("Bumpiness", s.Bumpiness, 0.1f, 20.0f, "%.2f");
        }
        if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* wraps[] = { "Repeat", "Clamp", "Mirror" };
            int wrap = (int)s.Wrap;
            if (UI::Combo("Wrap Mode", wrap, wraps, 3))
                s.Wrap = (TextureWrap)wrap;
            const char* filters[] = { "Point (no filter)", "Bilinear", "Trilinear" };
            int filter = (int)s.Filter;
            if (UI::Combo("Filter Mode", filter, filters, 3, "Point keeps pixel art crisp"))
                s.Filter = (TextureFilter)filter;
            ImGui::BeginDisabled(!s.Mipmaps || s.Filter == TextureFilter::Point);
            UI::SliderInt("Aniso Level", s.Anisotropy, 1, 16, "Sharper textures on surfaces seen at grazing angles");
            ImGui::EndDisabled();
            UI::Checkbox("Read/Write", s.ReadWrite, "Keeps the image's pixels readable in the game (scripts, generated sprite physics shapes). "
                                                    "Off: a crunched texture ships only as its .crn data (smaller build)");
            UI::Checkbox("Generate Mipmaps", s.Mipmaps, "Smaller copies used at a distance: removes shimmering on 3D surfaces");
            // Unity Streaming Mipmaps (Texture Streaming açıkken: Project Settings > Quality)
            ImGui::BeginDisabled(!s.Mipmaps);
            UI::Checkbox("Streaming Mipmaps", s.StreamingMipmaps,
                         "With Texture Streaming on (Project Settings > Quality), only the mip levels the cameras need are loaded.");
            if (s.StreamingMipmaps)
                UI::SliderInt("Mip Map Priority", s.StreamingPriority, -128, 127, "Over the memory budget, lower priority textures drop levels first.");
            ImGui::EndDisabled();
        }

        // Platform sekmeleri (Unity: Default / PC / Web ...)
        ImGui::Spacing();
        if (ImGui::BeginTabBar("##TexturePlatforms"))
        {
            auto platformFields = [](int& maxSize, TextureResizeAlgorithm& resize, TextureFormatChoice& format, TextureCompression& compression, bool& crunch, int& crunchQuality) {
                const char* sizes[] = { "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384" };
                int sizeIndex = 0;
                for (int i = 0; i < 10; ++i)
                    if (std::atoi(sizes[i]) == maxSize) sizeIndex = i;
                if (UI::Combo("Max Size", sizeIndex, sizes, 10, "Larger images are downscaled on import"))
                    maxSize = std::atoi(sizes[sizeIndex]);
                const char* resizes[] = { "Mitchell", "Bilinear" };
                int r = (int)resize;
                if (UI::Combo("Resize Algorithm", r, resizes, 2, "Used when the image is larger than Max Size"))
                    resize = (TextureResizeAlgorithm)r;
                const char* formats[] = { "Automatic", "RGBA 32 bit", "RGB 24 bit", "R 8 bit" };
                int f = (int)format;
                if (UI::Combo("Format", f, formats, 4, "GPU pixel format. Automatic picks one from the image and Compression"))
                    format = (TextureFormatChoice)f;
                ImGui::BeginDisabled(format != TextureFormatChoice::Automatic);
                const char* compressions[] = { "None", "Low Quality", "Normal Quality", "High Quality" };
                int c = (int)compression;
                if (UI::Combo("Compression", c, compressions, 4, "GPU block compression: less video memory. Low / Normal: DXT1 or DXT5, High: BC7"))
                    compression = (TextureCompression)c;
                ImGui::BeginDisabled(compression == TextureCompression::None);
                UI::Checkbox("Use Crunch Compression", crunch, "Crunch (crnlib, like Unity): when the game is built the texture is encoded to DXT1 / DXT5 blocks and "
                                                              "crunched into a small .crn file; the game unpacks it straight to the GPU format (smaller build, faster loading)");
                if (crunch)
                    UI::SliderInt("Compressor Quality", crunchQuality, 0, 100, "Higher: better image, larger file");
                ImGui::EndDisabled();
                ImGui::EndDisabled();
            };
            if (ImGui::BeginTabItem("Default"))
            {
                platformFields(s.MaxSize, s.Resize, s.Format, s.Compression, s.UseCrunch, s.CrunchQuality);
                ImGui::EndTabItem();
            }
            static const char* const kPlatformTabs[] = { ICON_FA_DESKTOP " PC", ICON_FA_GLOBE " Web", ICON_FA_MOBILE_SCREEN " Mobile" };
            static const char* const kPlatformNames[] = { "Override for PC", "Override for Web", "Override for Mobile" };
            for (int i = 0; i < kTexturePlatformCount; ++i)
                if (ImGui::BeginTabItem(kPlatformTabs[i]))
                {
                    auto& o = s.Platforms[i];
                    const bool before = o.Override;
                    UI::Checkbox(kPlatformNames[i], o.Override, "Use different settings on this platform");
                    if (o.Override && !before)
                    {
                        o.MaxSize = s.MaxSize;
                        o.Resize = s.Resize;
                        o.Format = s.Format;
                        o.Compression = s.Compression;
                        o.UseCrunch = s.UseCrunch;
                        o.CrunchQuality = s.CrunchQuality;
                    }
                    ImGui::BeginDisabled(!o.Override);
                    platformFields(o.MaxSize, o.Resize, o.Format, o.Compression, o.UseCrunch, o.CrunchQuality);
                    ImGui::EndDisabled();
                    ImGui::EndTabItem();
                }
            ImGui::EndTabBar();
        }

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

        // ---- İçe aktarma adımları (Unity: Asset PostProcessors; bu görselde çalışan adımlar)
        if (ImGui::CollapsingHeader("Asset PostProcessors"))
        {
            const TextureImportSettings& applied = st.Original;
            std::vector<const char*> steps = { "Conqueror.Import.TextureImporter" };
            if (applied.Mipmaps)
                steps.push_back("Conqueror.Import.MipmapGenerator");
            if (applied.Type == TextureType::NormalMap && applied.NormalFromGrayscale)
                steps.push_back("Conqueror.Import.NormalFromGrayscale");
            if (applied.Format == TextureFormatChoice::Automatic && applied.Compression != TextureCompression::None)
                steps.push_back("Conqueror.Import.BlockCompressor");
            if (applied.Type == TextureType::Sprite)
            {
                steps.push_back("Conqueror.Sprites.SpriteSheetImporter");
                if (applied.TightMesh)
                    steps.push_back("Conqueror.Sprites.TightMeshGenerator");
                if (applied.GeneratePhysicsShape)
                    steps.push_back("Conqueror.Sprites.PhysicsShapeGenerator");
            }
            for (const char* name : steps)
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextUnformatted(name);
            }
        }

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
        // Unity'deki önizleme altı satırı: 2048x880 (NPOT)  RGB Compressed DXT1|BC1 sRGB  0.9 MB
        if (live)
        {
            const uint32_t iw = live->GetWidth(), ih = live->GetHeight();
            const bool npot = (iw & (iw - 1)) != 0 || (ih & (ih - 1)) != 0;
            const std::string line = std::to_string(iw) + "x" + std::to_string(ih) + (npot ? " (NPOT)  " : "  ") + live->GetFormatName() +
                                     (live->GetImportSettings().Type == TextureType::NormalMap ? "" : " sRGB") + "  " + FormatBytes(live->GetMemoryBytes());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(line.c_str()).x) * 0.5f));
            ImGui::TextUnformatted(line.c_str());
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

    // Unity Inspector başlığı: Open (dış uygulamada açar; kaydedince yeniden içe aktarılır) ve sürüm denetimi (Check Out, Lock / Unlock,
    // Revert) düğmeleri, dosyanın durumu
    static void DrawAssetHeaderBar(const std::string& path)
    {
        namespace VC = VersionControl;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
        if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE "  Open"))
        {
#if defined(CQ_PLATFORM_WINDOWS)
            const std::string command = "start \"\" \"" + path + "\"";
#elif defined(CQ_PLATFORM_MACOS)
            const std::string command = "open \"" + path + "\"";
#else
            const std::string command = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
#endif
            const int rc = std::system(command.c_str());
            (void)rc;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Open in the default application; saving it there reimports the asset");
        if (VC::IsEnabled())
        {
            static std::string s_Message;
            const VC::FileStatus st = VC::Get(path);
            const bool perforce = VC::Mode() == 2;
            const bool versioned = st.Status != VC::State::NotVersioned && st.Status != VC::State::Unknown;
            ImGui::SameLine();
            if (!versioned)
            {
                if (ImGui::Button(ICON_FA_PLUS "  Add"))
                    VC::Add(path, &s_Message);
            }
            else
            {
                if (perforce && st.Status == VC::State::Clean)
                {
                    if (ImGui::Button(ICON_FA_CIRCLE_CHECK "  Check Out"))
                        VC::CheckOut(path, &s_Message);
                    ImGui::SameLine();
                }
                if (st.Status == VC::State::LockedLocal)
                {
                    if (ImGui::Button(ICON_FA_LOCK_OPEN "  Unlock"))
                        VC::Unlock(path, &s_Message);
                }
                else
                {
                    ImGui::BeginDisabled(st.Status == VC::State::LockedOther);
                    if (ImGui::Button(ICON_FA_LOCK "  Lock"))
                        VC::Lock(path, &s_Message);
                    ImGui::EndDisabled();
                }
                if (st.Status == VC::State::Modified || st.Status == VC::State::CheckedOut || st.Status == VC::State::Added ||
                    (perforce && st.Status == VC::State::LockedLocal))
                {
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Revert"))
                        VC::Revert(path, &s_Message);
                }
            }
            ImGui::SameLine();
            const bool warn = st.Status == VC::State::LockedOther || st.Status == VC::State::CheckedOutOther;
            ImGui::TextColored(warn ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.6f, 0.62f, 0.66f, 1.0f), "%s%s%s  (%s)", VC::StateName(st.Status),
                               st.Owner.empty() ? "" : ": ", st.Owner.c_str(), VC::ModeName());
            if (!s_Message.empty())
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", s_Message.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        // Unity Inspector altındaki AssetBundle alanı (yalnız Assets altındaki varlıklar)
        if (fs::path(path).generic_string().find("/Assets/") != std::string::npos)
        {
            const std::string own = AssetBundleBuilder::GetAssignedName(path);
            const std::string inherited = own.empty() ? AssetBundleBuilder::GetBundleName(path) : std::string();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(ICON_FA_BOX_ARCHIVE "  AssetBundle");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x));
            const std::string preview = !own.empty() ? own : (!inherited.empty() ? inherited + " (folder)" : std::string("None"));
            static char s_NewName[64] = "";
            bool openNew = false;
            if (ImGui::BeginCombo("##assetbundle", preview.c_str()))
            {
                if (ImGui::Selectable("None", own.empty()))
                    AssetBundleBuilder::SetBundleName(path, "");
                for (const auto& name : AssetBundleBuilder::GetAllBundleNames())
                    if (ImGui::Selectable(name.c_str(), name == own))
                        AssetBundleBuilder::SetBundleName(path, name);
                ImGui::Separator();
                if (ImGui::Selectable(ICON_FA_PLUS "  New..."))
                    openNew = true;
                ImGui::EndCombo();
            }
            if (openNew)
            {
                s_NewName[0] = '\0';
                ImGui::OpenPopup("##newbundle");
            }
            if (ImGui::BeginPopup("##newbundle"))
            {
                ImGui::TextUnformatted("New AssetBundle name");
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();
                const bool enter = ImGui::InputText("##bundlename", s_NewName, sizeof(s_NewName), ImGuiInputTextFlags_EnterReturnsTrue);
                if ((enter || ImGui::Button("Create")) && s_NewName[0])
                {
                    AssetBundleBuilder::SetBundleName(path, s_NewName);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Separator();
        }
    }

    void Draw(const std::string& path)
    {
        DrawAssetHeaderBar(path);
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
