#include "ExtraTextures.h"

#include "Core/Assets/SimpleAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <IconsFontAwesome6.h>
#include <glad/glad.h>
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<SceneCaptureCubeComponent>(Entity, SceneCaptureCubeComponent&) {}

    namespace ExtraTextures
    {
        namespace
        {
            namespace fs = std::filesystem;

            struct Entry
            {
                fs::file_time_type Time;
                uint32_t Handle = 0;
                int Size = 0;
            };

            std::unordered_map<std::string, Entry>& Cache()
            {
                static auto* c = new std::unordered_map<std::string, Entry>();
                return *c;
            }

            uint32_t Cached(const std::string& path, GLenum target, const std::function<bool(Entry&)>& build, int* size = nullptr)
            {
                const std::string file = SceneSerializer::ResolveAssetPath(path);
                std::error_code ec;
                const auto time = fs::last_write_time(file, ec);
                if (ec)
                    return 0;
                auto it = Cache().find(file);
                if (it != Cache().end() && it->second.Time == time)
                {
                    if (size) *size = it->second.Size;
                    return it->second.Handle;
                }
                if (it != Cache().end() && it->second.Handle)
                    glDeleteTextures(1, &it->second.Handle);
                Entry e;
                e.Time = time;
                glGenTextures(1, &e.Handle);
                glBindTexture(target, e.Handle);
                if (!build(e))
                {
                    glDeleteTextures(1, &e.Handle);
                    e.Handle = 0;
                }
                glBindTexture(target, 0);
                Cache()[file] = e;
                if (size) *size = e.Size;
                return e.Handle;
            }

            struct Image
            {
                int W = 0, H = 0;
                std::vector<uint8_t> Pixels;
            };

            bool LoadImage(const std::string& path, Image& out, int size)
            {
                int w, h, n;
                stbi_set_flip_vertically_on_load(false);
                stbi_uc* data = stbi_load(SceneSerializer::ResolveAssetPath(path).c_str(), &w, &h, &n, 4);
                if (!data)
                    return false;
                out.W = size > 0 ? size : w;
                out.H = size > 0 ? size : h;
                out.Pixels.resize((size_t)out.W * out.H * 4);
                if (out.W == w && out.H == h)
                    std::memcpy(out.Pixels.data(), data, out.Pixels.size());
                else
                    stbir_resize_uint8_linear(data, w, h, 0, out.Pixels.data(), out.W, out.H, 0, STBIR_RGBA);
                stbi_image_free(data);
                return true;
            }
        }

        uint32_t TextureArray(const std::string& path)
        {
            return Cached(path, GL_TEXTURE_2D_ARRAY, [&](Entry& e) {
                SimpleAssets::Data data;
                if (!SimpleAssets::Load(path, data))
                    return false;
                const auto& rows = data.Table("Slices");
                if (rows.empty())
                    return false;
                const int size = std::clamp(data.Int("Size", 512), 4, 4096);
                std::vector<Image> images(rows.size());
                for (size_t i = 0; i < rows.size(); ++i)
                    if (!LoadImage(SimpleAssets::Cell(rows[i], 0), images[i], size))
                    {
                        images[i].W = images[i].H = size;
                        images[i].Pixels.assign((size_t)size * size * 4, 255);
                    }
                glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, size, size, (GLsizei)rows.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                for (size_t i = 0; i < rows.size(); ++i)
                    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, (GLint)i, size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, images[i].Pixels.data());
                glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                e.Size = (int)rows.size();
                return true;
            });
        }

        uint32_t Volume(const std::string& path)
        {
            return Cached(path, GL_TEXTURE_3D, [&](Entry& e) {
                SimpleAssets::Data data;
                Image sheet;
                if (!SimpleAssets::Load(path, data) || !LoadImage(data.Str("Source"), sheet, 0))
                    return false;
                const int columns = std::max(data.Int("Columns", 4), 1), rows = std::max(data.Int("Rows", 4), 1);
                const int cw = sheet.W / columns, ch = sheet.H / rows;
                const int depth = columns * rows;
                if (cw <= 0 || ch <= 0)
                    return false;
                std::vector<uint8_t> voxels((size_t)cw * ch * depth * 4);
                for (int d = 0; d < depth; ++d)
                {
                    const int ox = (d % columns) * cw, oy = (d / columns) * ch;
                    for (int y = 0; y < ch; ++y)
                        std::memcpy(voxels.data() + ((size_t)d * ch + y) * cw * 4, sheet.Pixels.data() + ((size_t)oy + y) * sheet.W * 4 + (size_t)ox * 4, (size_t)cw * 4);
                }
                glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, cw, ch, depth, 0, GL_RGBA, GL_UNSIGNED_BYTE, voxels.data());
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
                e.Size = depth;
                return true;
            });
        }

        uint32_t Cube(const std::string& path, int* size)
        {
            return Cached(path, GL_TEXTURE_CUBE_MAP, [&](Entry& e) {
                SimpleAssets::Data data;
                if (!SimpleAssets::Load(path, data))
                    return false;
                const int s = std::clamp(data.Int("Size", 256), 16, 2048);
                int mips = 1;
                for (int sz = s; sz > 1; sz >>= 1) ++mips;
                glTexStorage2D(GL_TEXTURE_CUBE_MAP, mips, GL_RGBA16F, s, s);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                // İlk yakalamaya kadar siyah (ayrılmış ama yazılmamış içerik tanımsızdır)
                const std::vector<float> black((size_t)s * s * 4, 0.0f);
                for (int face = 0; face < 6; ++face)
                    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, s, s, GL_RGBA, GL_FLOAT, black.data());
                glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
                e.Size = s;
                return true;
            }, size);
        }

        void Shutdown()
        {
            for (auto& [path, e] : Cache())
                if (e.Handle)
                    glDeleteTextures(1, &e.Handle);
            Cache().clear();
        }

        void RegisterComponents()
        {
            using C = SceneCaptureCubeComponent;
            ComponentDef<C>("Scene Capture Cube", "SceneCaptureCubeComponent", "Rendering", ICON_FA_CUBE)
                .Describe("Renders the scene around this object into a Cube Render Target (.cqcubert), for Cube Render Target Sample nodes in Shader Graphs.")
                .Field("Target", "Target", &C::Target, FieldKind::Asset).Filter(".cqcubert")
                .Field("Interval", "Interval", &C::Interval).Tooltip("Seconds between captures; 0 captures once when the game starts.");
        }
    }

    namespace MaterialParameterCollections
    {
        namespace
        {
            std::unordered_map<std::string, std::unordered_map<std::string, glm::vec4>>& Overrides()
            {
                static std::unordered_map<std::string, std::unordered_map<std::string, glm::vec4>> o;
                return o;
            }

            std::string Key(const std::string& path)
            {
                return SceneSerializer::ResolveAssetPath(path);
            }
        }

        bool GetScalar(const std::string& path, const std::string& name, float& out)
        {
            auto it = Overrides().find(Key(path));
            if (it != Overrides().end())
                if (auto v = it->second.find(name); v != it->second.end())
                {
                    out = v->second.x;
                    return true;
                }
            SimpleAssets::Data data;
            if (!SimpleAssets::Load(path, data))
                return false;
            for (const auto& row : data.Table("Scalars"))
                if (SimpleAssets::Cell(row, 0) == name)
                {
                    out = SimpleAssets::CellFloat(row, 1);
                    return true;
                }
            return false;
        }

        bool GetVector(const std::string& path, const std::string& name, glm::vec4& out)
        {
            auto it = Overrides().find(Key(path));
            if (it != Overrides().end())
                if (auto v = it->second.find(name); v != it->second.end())
                {
                    out = v->second;
                    return true;
                }
            SimpleAssets::Data data;
            if (!SimpleAssets::Load(path, data))
                return false;
            for (const auto& row : data.Table("Vectors"))
                if (SimpleAssets::Cell(row, 0) == name)
                {
                    out = glm::vec4(SimpleAssets::CellFloat(row, 1), SimpleAssets::CellFloat(row, 2), SimpleAssets::CellFloat(row, 3), SimpleAssets::CellFloat(row, 4, 1.0f));
                    return true;
                }
            return false;
        }

        void SetScalar(const std::string& path, const std::string& name, float value)
        {
            Overrides()[Key(path)][name] = glm::vec4(value);
        }

        void SetVector(const std::string& path, const std::string& name, const glm::vec4& value)
        {
            Overrides()[Key(path)][name] = value;
        }

        void ResetOverrides()
        {
            Overrides().clear();
        }
    }
}
