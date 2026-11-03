#include "ComputeShaders.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"

#include <glad/glad.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace Conqueror::ComputeShaders
{
    namespace fs = std::filesystem;

    namespace
    {
        struct Param
        {
            int Type = 0;   // 0 float, 1 int, 2 vec4, 3 texture
            float F = 0.0f; int I = 0; glm::vec4 V{ 0.0f };
            std::shared_ptr<Texture2D> Tex;
        };
        struct Program
        {
            GLuint Id = 0;
            fs::file_time_type Time{};
            std::string Error;
            std::map<std::string, Param> Params;
        };
        std::unordered_map<std::string, Program>& Programs()
        {
            static auto* m = new std::unordered_map<std::string, Program>();
            return *m;
        }

        Program& Get(const std::string& shader)
        {
            const std::string file = SceneSerializer::ResolveAssetPath(shader);
            Program& p = Programs()[file];
            std::error_code ec;
            const auto t = fs::last_write_time(file, ec);
            if (ec)
            {
                p.Error = "Compute shader not found: " + shader;
                return p;
            }
            if (t == p.Time && (p.Id || !p.Error.empty()))
                return p;
            p.Time = t;
            if (p.Id)
                glDeleteProgram(p.Id);
            p.Id = 0;
            p.Error.clear();
            if (!GLAD_GL_VERSION_4_3)
            {
                p.Error = "Compute shaders need OpenGL 4.3 or newer";
                return p;
            }
            std::ifstream f(file);
            std::stringstream ss;
            ss << f.rdbuf();
            std::string src = ss.str();
            if (src.find("#version") == std::string::npos)
                src = "#version 450 core\n" + src;
            GLuint s = glCreateShader(GL_COMPUTE_SHADER);
            const char* c = src.c_str();
            glShaderSource(s, 1, &c, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[4096];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                p.Error = log;
                glDeleteShader(s);
                CQ_CORE_ERROR("[Compute] {0}: {1}", fs::path(file).filename().string(), p.Error);
                return p;
            }
            p.Id = glCreateProgram();
            glAttachShader(p.Id, s);
            glLinkProgram(p.Id);
            glDeleteShader(s);
            glGetProgramiv(p.Id, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                char log[4096];
                glGetProgramInfoLog(p.Id, sizeof(log), nullptr, log);
                p.Error = log;
                glDeleteProgram(p.Id);
                p.Id = 0;
            }
            return p;
        }

        bool IsImageType(GLenum t)
        {
            return t == GL_IMAGE_2D || t == GL_INT_IMAGE_2D || t == GL_UNSIGNED_INT_IMAGE_2D;
        }
    }

    bool SetTexture(const std::string& shader, const std::string& name, const std::string& texturePath)
    {
        Program& p = Get(shader);
        Param& prm = p.Params[name];
        prm.Type = 3;
        prm.Tex = texturePath.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(texturePath));
        return prm.Tex != nullptr;
    }

    bool SetFloat(const std::string& shader, const std::string& name, float value)
    {
        Param& prm = Get(shader).Params[name];
        prm.Type = 0;
        prm.F = value;
        return true;
    }

    bool SetInt(const std::string& shader, const std::string& name, int value)
    {
        Param& prm = Get(shader).Params[name];
        prm.Type = 1;
        prm.I = value;
        return true;
    }

    bool SetVector(const std::string& shader, const std::string& name, const glm::vec4& value)
    {
        Param& prm = Get(shader).Params[name];
        prm.Type = 2;
        prm.V = value;
        return true;
    }

    bool Dispatch(const std::string& shader, int gx, int gy, int gz)
    {
        Program& p = Get(shader);
        if (!p.Id)
            return false;
        GLint prevProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        glUseProgram(p.Id);
        int imageUnit = 0, textureUnit = 8;
        for (auto& [name, prm] : p.Params)
        {
            const GLint loc = glGetUniformLocation(p.Id, name.c_str());
            if (loc < 0)
                continue;
            switch (prm.Type)
            {
                case 0: glUniform1f(loc, prm.F); break;
                case 1: glUniform1i(loc, prm.I); break;
                case 2: glUniform4f(loc, prm.V.x, prm.V.y, prm.V.z, prm.V.w); break;
                case 3:
                {
                    if (!prm.Tex || !prm.Tex->GetRendererID())
                        break;
                    // Uniform türü: image2D -> görüntü birimi (yazılabilir), sampler2D -> doku birimi
                    GLuint index = glGetProgramResourceIndex(p.Id, GL_UNIFORM, name.c_str());
                    GLenum props[] = { GL_TYPE };
                    GLint type = 0;
                    if (index != GL_INVALID_INDEX)
                        glGetProgramResourceiv(p.Id, GL_UNIFORM, index, 1, props, 1, nullptr, &type);
                    if (IsImageType((GLenum)type))
                    {
                        glBindImageTexture((GLuint)imageUnit, prm.Tex->GetRendererID(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
                        glUniform1i(loc, imageUnit++);
                    }
                    else
                    {
                        glBindTextureUnit((GLuint)textureUnit, prm.Tex->GetRendererID());
                        glUniform1i(loc, textureUnit++);
                    }
                    break;
                }
            }
        }
        glDispatchCompute((GLuint)std::max(gx, 1), (GLuint)std::max(gy, 1), (GLuint)std::max(gz, 1));
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        glUseProgram((GLuint)prevProgram);
        return true;
    }

    std::string GetError(const std::string& shader)
    {
        return Get(shader).Error;
    }

    Info Inspect(const std::string& shader)
    {
        Info info;
        Program& p = Get(shader);
        info.Error = p.Error;
        if (!p.Id)
            return info;
        info.Compiled = true;
        GLint size[3] = { 0, 0, 0 };
        glGetProgramiv(p.Id, GL_COMPUTE_WORK_GROUP_SIZE, size);
        info.GroupSize = { size[0], size[1], size[2] };
        GLint count = 0;
        glGetProgramiv(p.Id, GL_ACTIVE_UNIFORMS, &count);
        for (GLint i = 0; i < count; ++i)
        {
            char name[256];
            GLint n = 0;
            GLenum type = 0;
            glGetActiveUniform(p.Id, (GLuint)i, sizeof(name), nullptr, &n, &type, name);
            const char* t = type == GL_FLOAT ? "float" : type == GL_INT ? "int" : type == GL_FLOAT_VEC4 ? "vec4" : type == GL_FLOAT_VEC2 ? "vec2"
                          : type == GL_FLOAT_VEC3 ? "vec3" : IsImageType(type) ? "image2D (write)" : type == GL_SAMPLER_2D ? "sampler2D (read)" : "other";
            info.Uniforms.push_back({ name, t });
        }
        return info;
    }

    void Shutdown()
    {
        for (auto& [k, p] : Programs())
            if (p.Id)
                glDeleteProgram(p.Id);
        Programs().clear();
    }
}
