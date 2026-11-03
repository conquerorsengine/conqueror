#include "CustomRenderTexture.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Time/TimeManager.h"
#include "Renderer/RenderTargetRegistry.h"
#include "Renderer/RHI/Texture.h"

#include <glad/glad.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Conqueror::CustomRenderTextures
{
    namespace fs = std::filesystem;

    namespace
    {
        struct Instance
        {
            std::string Path;               // çözülmüş dosya yolu
            CustomRenderTextureAsset Asset;
            fs::file_time_type AssetTime{}, ShaderTime{};
            std::shared_ptr<RenderTargetTexture> Target, Previous;
            GLuint Program = 0;
            std::string Error;
            bool NeedsInit = true, NeedsUpdate = true;
            double LastUpdate = -1e9;
            uint64_t LastFrame = ~0ull;
            uint64_t Updates = 0;
            std::shared_ptr<Texture2D> InitTex;
        };

        std::unordered_map<std::string, Instance>& Instances()
        {
            static auto* m = new std::unordered_map<std::string, Instance>();   // GL nesneleri Shutdown'da bırakılır
            return *m;
        }

        GLuint s_Vao = 0;

        std::string Key(const std::string& path) { return SceneSerializer::ResolveAssetPath(path); }

        GLuint Compile(GLenum type, const std::string& src, std::string& error)
        {
            GLuint s = glCreateShader(type);
            const char* c = src.c_str();
            glShaderSource(s, 1, &c, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[2048];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                error = log;
                glDeleteShader(s);
                return 0;
            }
            return s;
        }

        bool BuildProgram(Instance& in)
        {
            if (in.Program)
            {
                glDeleteProgram(in.Program);
                in.Program = 0;
            }
            in.Error.clear();
            std::string user;
            if (!in.Asset.Shader.empty())
            {
                std::ifstream f(SceneSerializer::ResolveAssetPath(in.Asset.Shader));
                if (!f)
                {
                    in.Error = "Shader file not found: " + in.Asset.Shader;
                    return false;
                }
                std::stringstream ss;
                ss << f.rdbuf();
                user = ss.str();
            }
            else
                user = "vec4 Update(vec2 uv) { return texture(u_SelfTexture, uv); }\n";
            static const char* vs =
                "#version 450 core\n"
                "out vec2 v_UV;\n"
                "void main() {\n"
                "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
                "    v_UV = p;\n"
                "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                "}\n";
            const std::string fs =
                "#version 450 core\n"
                "in vec2 v_UV;\n"
                "layout(location = 0) out vec4 o_Color;\n"
                "uniform float u_Time;\nuniform float u_DeltaTime;\nuniform vec2 u_Resolution;\nuniform int u_FrameCount;\n"
                "uniform sampler2D u_SelfTexture;\nuniform sampler2D u_InitTexture;\n"
                "#line 1\n" + user +
                "\nvoid main() { o_Color = Update(v_UV); }\n";
            std::string err;
            GLuint v = Compile(GL_VERTEX_SHADER, vs, err);
            GLuint f = v ? Compile(GL_FRAGMENT_SHADER, fs, err) : 0;
            if (!v || !f)
            {
                if (v) glDeleteShader(v);
                in.Error = err;
                CQ_CORE_ERROR("[CustomRenderTexture] {0}: {1}", fs::path(in.Path).filename().string(), err);
                return false;
            }
            GLuint p = glCreateProgram();
            glAttachShader(p, v);
            glAttachShader(p, f);
            glLinkProgram(p);
            glDeleteShader(v);
            glDeleteShader(f);
            GLint ok = 0;
            glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                char log[2048];
                glGetProgramInfoLog(p, sizeof(log), nullptr, log);
                in.Error = log;
                glDeleteProgram(p);
                return false;
            }
            in.Program = p;
            return true;
        }

        void SetWrap(const std::shared_ptr<RenderTargetTexture>& t, bool repeat)
        {
            if (!t || !t->GetRendererID())
                return;
            const GLint w = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
            glTextureParameteri(t->GetRendererID(), GL_TEXTURE_WRAP_S, w);
            glTextureParameteri(t->GetRendererID(), GL_TEXTURE_WRAP_T, w);
        }

        void Refresh(Instance& in)
        {
            std::error_code ec;
            const auto at = fs::last_write_time(in.Path, ec);
            if (!ec && at != in.AssetTime)
            {
                in.AssetTime = at;
                CustomRenderTextureAsset a;
                if (Load(in.Path, a))
                {
                    const bool resized = a.Width != in.Asset.Width || a.Height != in.Asset.Height;
                    const bool shaderChanged = a.Shader != in.Asset.Shader;
                    in.Asset = a;
                    in.Target->Resize((uint32_t)a.Width, (uint32_t)a.Height);
                    in.Previous->Resize((uint32_t)a.Width, (uint32_t)a.Height);
                    SetWrap(in.Target, a.Repeat);
                    SetWrap(in.Previous, a.Repeat);
                    in.InitTex = a.InitTexture.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(a.InitTexture));
                    if (resized || shaderChanged || !in.Program)
                        in.ShaderTime = {};
                    in.NeedsInit = in.NeedsUpdate = true;
                }
            }
            if (!in.Asset.Shader.empty())
            {
                const auto st = fs::last_write_time(SceneSerializer::ResolveAssetPath(in.Asset.Shader), ec);
                if (!ec && st != in.ShaderTime)
                {
                    in.ShaderTime = st;
                    BuildProgram(in);
                    in.NeedsUpdate = true;
                }
            }
            else if (!in.Program)
                BuildProgram(in);
        }

        void Initialize(Instance& in)
        {
            GLint prevFbo = 0, vp[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
            glGetIntegerv(GL_VIEWPORT, vp);
            for (auto& t : { in.Target, in.Previous })
            {
                glBindFramebuffer(GL_FRAMEBUFFER, t->GetFramebuffer());
                glViewport(0, 0, (GLsizei)t->GetWidth(), (GLsizei)t->GetHeight());
                const glm::vec4& c = in.Asset.InitColor;
                glClearColor(c.r, c.g, c.b, c.a);
                glClear(GL_COLOR_BUFFER_BIT);
                if (in.InitTex && in.InitTex->GetRendererID())
                {
                    // Başlangıç dokusu: çerçeve tamponuna kopyala (boyuta ölçekler)
                    GLuint readFbo = 0;
                    glGenFramebuffers(1, &readFbo);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
                    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, in.InitTex->GetRendererID(), 0);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, t->GetFramebuffer());
                    glBlitFramebuffer(0, 0, (GLint)in.InitTex->GetWidth(), (GLint)in.InitTex->GetHeight(), 0, 0, (GLint)t->GetWidth(),
                                      (GLint)t->GetHeight(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glDeleteFramebuffers(1, &readFbo);
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
            glViewport(vp[0], vp[1], vp[2], vp[3]);
            in.NeedsInit = false;
        }

        void Render(Instance& in, float dt)
        {
            if (!in.Program)
                return;
            // Çift tampon: önceki kare -> Previous
            if (in.Asset.DoubleBuffered)
                glCopyImageSubData(in.Target->GetRendererID(), GL_TEXTURE_2D, 0, 0, 0, 0, in.Previous->GetRendererID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                                   (GLsizei)in.Target->GetWidth(), (GLsizei)in.Target->GetHeight(), 1);
            GLint prevFbo = 0, prevProgram = 0, vp[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
            glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
            glGetIntegerv(GL_VIEWPORT, vp);
            const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glBindFramebuffer(GL_FRAMEBUFFER, in.Target->GetFramebuffer());
            glViewport(0, 0, (GLsizei)in.Target->GetWidth(), (GLsizei)in.Target->GetHeight());
            glUseProgram(in.Program);
            glUniform1f(glGetUniformLocation(in.Program, "u_Time"), TimeManager::GetTime());
            glUniform1f(glGetUniformLocation(in.Program, "u_DeltaTime"), dt);
            glUniform2f(glGetUniformLocation(in.Program, "u_Resolution"), (float)in.Target->GetWidth(), (float)in.Target->GetHeight());
            glUniform1i(glGetUniformLocation(in.Program, "u_FrameCount"), (int)in.Updates);
            glBindTextureUnit(0, in.Asset.DoubleBuffered ? in.Previous->GetRendererID() : in.Target->GetRendererID());
            glUniform1i(glGetUniformLocation(in.Program, "u_SelfTexture"), 0);
            glBindTextureUnit(1, in.InitTex ? in.InitTex->GetRendererID() : in.Previous->GetRendererID());
            glUniform1i(glGetUniformLocation(in.Program, "u_InitTexture"), 1);
            if (!s_Vao)
                glCreateVertexArrays(1, &s_Vao);
            glBindVertexArray(s_Vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glUseProgram((GLuint)prevProgram);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
            glViewport(vp[0], vp[1], vp[2], vp[3]);
            if (depth) glEnable(GL_DEPTH_TEST);
            if (blend) glEnable(GL_BLEND);
            if (cull) glEnable(GL_CULL_FACE);
            ++in.Updates;
            in.Target->RenderCount++;
        }
    }

    bool IsAsset(const std::string& path)
    {
        std::string e = fs::path(path).extension().string();
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return e == Extension;
    }

    bool Load(const std::string& path, CustomRenderTextureAsset& a)
    {
        try
        {
            YAML::Node n = YAML::LoadFile(Key(path))["CustomRenderTexture"];
            if (!n)
                return false;
            a = {};
            if (n["Width"]) a.Width = std::clamp(n["Width"].as<int>(), 1, 4096);
            if (n["Height"]) a.Height = std::clamp(n["Height"].as<int>(), 1, 4096);
            if (n["Shader"]) a.Shader = n["Shader"].as<std::string>();
            if (n["UpdateMode"]) a.UpdateMode = std::clamp(n["UpdateMode"].as<int>(), 0, 2);
            if (n["UpdatePeriod"]) a.UpdatePeriod = std::max(0.0f, n["UpdatePeriod"].as<float>());
            if (auto c = n["InitColor"]; c && c.IsSequence() && c.size() == 4)
                a.InitColor = { c[0].as<float>(), c[1].as<float>(), c[2].as<float>(), c[3].as<float>() };
            if (n["InitTexture"]) a.InitTexture = n["InitTexture"].as<std::string>();
            if (n["DoubleBuffered"]) a.DoubleBuffered = n["DoubleBuffered"].as<bool>();
            if (n["Repeat"]) a.Repeat = n["Repeat"].as<bool>();
            return true;
        }
        catch (...) { return false; }
    }

    bool Save(const std::string& path, const CustomRenderTextureAsset& a)
    {
        YAML::Emitter e;
        e << YAML::BeginMap << YAML::Key << "CustomRenderTexture" << YAML::Value << YAML::BeginMap;
        e << YAML::Key << "Width" << YAML::Value << a.Width;
        e << YAML::Key << "Height" << YAML::Value << a.Height;
        e << YAML::Key << "Shader" << YAML::Value << a.Shader;
        e << YAML::Key << "UpdateMode" << YAML::Value << a.UpdateMode;
        e << YAML::Key << "UpdatePeriod" << YAML::Value << a.UpdatePeriod;
        e << YAML::Key << "InitColor" << YAML::Value << YAML::Flow << YAML::BeginSeq << a.InitColor.r << a.InitColor.g << a.InitColor.b << a.InitColor.a << YAML::EndSeq;
        e << YAML::Key << "InitTexture" << YAML::Value << a.InitTexture;
        e << YAML::Key << "DoubleBuffered" << YAML::Value << a.DoubleBuffered;
        e << YAML::Key << "Repeat" << YAML::Value << a.Repeat;
        e << YAML::EndMap << YAML::EndMap;
        std::ofstream f(Key(path), std::ios::trunc);
        if (!f)
            return false;
        f << e.c_str() << "\n";
        return true;
    }

    std::shared_ptr<RenderTargetTexture> Acquire(const std::string& path)
    {
        const std::string key = Key(path);
        auto& map = Instances();
        auto it = map.find(key);
        if (it == map.end())
        {
            Instance in;
            in.Path = key;
            if (!Load(key, in.Asset))
                return nullptr;
            in.Target = RenderTargetRegistry::Acquire("crt:" + key);
            in.Previous = RenderTargetRegistry::Acquire("crt-prev:" + key);
            in.Target->Resize((uint32_t)in.Asset.Width, (uint32_t)in.Asset.Height);
            in.Previous->Resize((uint32_t)in.Asset.Width, (uint32_t)in.Asset.Height);
            in.Target->SetPath(key);
            it = map.emplace(key, std::move(in)).first;
            it->second.AssetTime = {};   // ilk Refresh ayarları uygular
            Refresh(it->second);
            Initialize(it->second);
        }
        return it->second.Target;
    }

    void UpdateAll()
    {
        const uint64_t frame = TimeManager::GetFrameCount();
        const double now = TimeManager::GetUnscaledTime();
        for (auto& [key, in] : Instances())
        {
            if (in.LastFrame == frame)
                continue;   // aynı karede ikinci kamera: tekrar güncelleme
            in.LastFrame = frame;
            Refresh(in);
            if (in.NeedsInit)
                Initialize(in);
            bool update = in.NeedsUpdate;
            if (in.Asset.UpdateMode == 1 && now - in.LastUpdate >= in.Asset.UpdatePeriod)
                update = true;
            if (!update)
                continue;
            const float dt = in.LastUpdate < -1e8 ? 0.0f : (float)(now - in.LastUpdate);
            Render(in, dt);
            in.LastUpdate = now;
            in.NeedsUpdate = false;
        }
    }

    void RequestUpdate(const std::string& path, bool initialize)
    {
        auto it = Instances().find(Key(path));
        if (it == Instances().end())
        {
            Acquire(path);
            it = Instances().find(Key(path));
            if (it == Instances().end())
                return;
        }
        it->second.NeedsUpdate = true;
        if (initialize)
            it->second.NeedsInit = true;
    }

    std::string GetError(const std::string& path)
    {
        auto it = Instances().find(Key(path));
        return it == Instances().end() ? std::string() : it->second.Error;
    }

    uint64_t GetUpdateCount(const std::string& path)
    {
        auto it = Instances().find(Key(path));
        return it == Instances().end() ? 0 : it->second.Updates;
    }

    void Shutdown()
    {
        for (auto& [key, in] : Instances())
            if (in.Program)
                glDeleteProgram(in.Program);
        Instances().clear();
        if (s_Vao)
            glDeleteVertexArrays(1, &s_Vao);
        s_Vao = 0;
    }
}
