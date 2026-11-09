#include "AssetPreview.h"

#include "Core/Logging/Log.h"
#include "Core/Time/TimeManager.h"
#include "Renderer/RenderTargetRegistry.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/SkinnedMesh.h"
#include "Renderer/RHI/VertexArray.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace Conqueror::Editor::AssetPreview
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr int kSize = 160;

        struct Entry
        {
            fs::file_time_type Time;
            std::shared_ptr<Texture2D> Texture;
            bool Done = false;
        };

        std::map<std::string, Entry>& Cache()
        {
            static auto& c = *new std::map<std::string, Entry>();
            return c;
        }

        uint32_t Program()
        {
            static uint32_t program = 0;
            if (program)
                return program;
            const char* vs = R"(#version 450 core
                layout(location = 0) in vec3 a_Position;
                layout(location = 1) in vec3 a_Normal;
                uniform mat4 u_VP;
                out vec3 v_Normal;
                void main() { v_Normal = a_Normal; gl_Position = u_VP * vec4(a_Position, 1.0); })";
            const char* fs = R"(#version 450 core
                in vec3 v_Normal;
                out vec4 o_Color;
                void main()
                {
                    vec3 N = normalize(v_Normal);
                    float key = max(dot(N, normalize(vec3(0.5, 0.8, 0.6))), 0.0);
                    float fill = max(dot(N, normalize(vec3(-0.6, 0.2, -0.3))), 0.0);
                    vec3 c = vec3(0.62, 0.66, 0.72) * (0.25 + 0.75 * key + 0.25 * fill);
                    o_Color = vec4(c, 1.0);
                })";
            const auto compile = [](GLenum type, const char* src) {
                uint32_t s = glCreateShader(type);
                glShaderSource(s, 1, &src, nullptr);
                glCompileShader(s);
                return s;
            };
            uint32_t a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
            program = glCreateProgram();
            glAttachShader(program, a);
            glAttachShader(program, b);
            glLinkProgram(program);
            glDeleteShader(a);
            glDeleteShader(b);
            return program;
        }

        bool Render(const fs::path& path, const std::shared_ptr<RenderTargetTexture>& target)
        {
            // Yerleşik ağ (.cqmesh): "Mesh: Cube" -> ilkel
            std::shared_ptr<Model> model;
            if (path.extension() == ".cqmesh")
            {
                std::ifstream in(path);
                std::string line, type;
                while (std::getline(in, line))
                    if (line.rfind("Mesh:", 0) == 0)
                        type = line.substr(line.find_first_not_of(" \t", 5));
                model = std::make_shared<Model>();
                static auto& primitives = *new std::map<std::string, std::shared_ptr<Mesh>>();
                auto& mesh = primitives[type];
                if (!mesh)
                    mesh = type == "Sphere" ? Mesh::CreateSphere() : type == "Plane" ? Mesh::CreatePlane() : type == "Cylinder" ? Mesh::CreateCylinder() : Mesh::CreateCube();
                model->Meshes.push_back(mesh);
            }
            else
                model = ModelLoader::Load(path.string());
            if (!model)
                return false;
            glm::vec3 lo(1e30f), hi(-1e30f);
            size_t vertices = 0;
            const auto grow = [&](const glm::vec3& p) { lo = glm::min(lo, p); hi = glm::max(hi, p); ++vertices; };
            for (const auto& m : model->Meshes)
                for (const auto& v : m->GetVertices())
                    grow(v.Position);
            for (const auto& m : model->SkinnedMeshes)
                for (const auto& v : m->GetVertices())
                    grow(v.Position);
            if (!vertices)
                return false;
            const glm::vec3 center = (lo + hi) * 0.5f;
            const float radius = std::max(glm::length(hi - lo) * 0.5f, 1e-4f);
            const glm::vec3 eye = center + glm::normalize(glm::vec3(0.8f, 0.55f, 1.0f)) * radius * 2.6f;
            const glm::mat4 vp = glm::perspective(glm::radians(35.0f), 1.0f, radius * 0.1f, radius * 8.0f) * glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));

            GLint fbo = 0, viewport[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            glGetIntegerv(GL_VIEWPORT, viewport);
            const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
            GLint program = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            uint32_t rbo = 0, frame = 0;
            glGenRenderbuffers(1, &rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, rbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kSize, kSize);
            glGenFramebuffers(1, &frame);
            glBindFramebuffer(GL_FRAMEBUFFER, frame);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->GetRendererID(), 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
            glViewport(0, 0, kSize, kSize);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.19f, 0.20f, 0.23f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glUseProgram(Program());
            glUniformMatrix4fv(glGetUniformLocation(Program(), "u_VP"), 1, GL_FALSE, glm::value_ptr(vp));
            for (const auto& m : model->Meshes)
            {
                m->Bind();
                glDrawElements(GL_TRIANGLES, (GLsizei)m->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
            }
            for (const auto& m : model->SkinnedMeshes)
            {
                m->GetVertexArray()->Bind();
                glDrawElements(GL_TRIANGLES, (GLsizei)m->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
            }
            glBindVertexArray(0);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
            glDeleteFramebuffers(1, &frame);
            glDeleteRenderbuffers(1, &rbo);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            glUseProgram((GLuint)program);
            if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
            if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
            return true;
        }
    }

    bool CanPreview(const fs::path& path)
    {
        std::string e = path.extension().string();
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return e == ".glb" || e == ".gltf" || e == ".fbx" || e == ".obj" || e == ".dae" || e == ".blend" || e == ".3ds" || e == ".cqmesh";
    }

    std::shared_ptr<Texture2D> Get(const fs::path& path)
    {
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (ec)
            return nullptr;
        const std::string key = path.string();
        auto& entry = Cache()[key];
        if (entry.Done && entry.Time == time)
            return entry.Texture;
        // Kare başına tek model: büyük klasörler arayüzü kilitlemesin
        static uint64_t s_Frame = 0;
        static uint64_t s_LastRendered = (uint64_t)-1;
        const uint64_t frame = (uint64_t)TimeManager::GetFrameCount();
        if (frame == s_LastRendered)
            return nullptr;
        (void)s_Frame;
        s_LastRendered = frame;
        auto target = RenderTargetRegistry::Acquire("__preview_" + std::to_string(std::hash<std::string>{}(key)));
        target->Resize((uint32_t)kSize, (uint32_t)kSize);
        entry.Time = time;
        entry.Done = true;
        entry.Texture = Render(path, target) ? std::static_pointer_cast<Texture2D>(target) : nullptr;
        return entry.Texture;
    }
}
