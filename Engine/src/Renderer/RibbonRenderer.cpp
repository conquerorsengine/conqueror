#include "RibbonRenderer.h"
#include "Core/Logging/Log.h"
#include "Renderer/Renderer.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        struct RibbonVertex
        {
            glm::vec3 Position;
            glm::vec2 UV;
            glm::vec4 Color;
        };

        struct State
        {
            std::shared_ptr<Conqueror::Shader> RibbonShader;
            GLuint VAO = 0, VBO = 0;
            size_t Capacity = 0;
            std::vector<RibbonVertex> Vertices;
            std::unordered_map<std::string, std::shared_ptr<Texture2D>> Textures;
            bool Failed = false;
        };

        State* s_State = nullptr;

        State* Get()
        {
            if (!s_State)
            {
                s_State = new State();
                s_State->RibbonShader = Shader::Create("Engine/src/Shaders/3D/Forward/Ribbon.cqsh");
                glCreateVertexArrays(1, &s_State->VAO);
                glBindVertexArray(s_State->VAO);
                glGenBuffers(1, &s_State->VBO);
                glBindBuffer(GL_ARRAY_BUFFER, s_State->VBO);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (const void*)offsetof(RibbonVertex, Position));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (const void*)offsetof(RibbonVertex, UV));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (const void*)offsetof(RibbonVertex, Color));
                glBindVertexArray(0);
                if (!s_State->RibbonShader || !s_State->RibbonShader->IsValid())
                {
                    CQ_CORE_ERROR("[Ribbon] shader failed to compile");
                    s_State->Failed = true;
                }
            }
            return s_State;
        }
    }

    std::shared_ptr<Texture2D> RibbonRenderer::LoadTexture(const std::string& path)
    {
        if (path.empty())
            return nullptr;
        State* st = Get();
        auto it = st->Textures.find(path);
        if (it != st->Textures.end())
            return it->second;
        auto tex = Texture2D::Create(path);
        st->Textures[path] = tex;
        return tex;
    }

    void RibbonRenderer::Draw(const std::vector<Point>& input, const Params& params)
    {
        if (input.size() < 2)
            return;
        State* st = Get();
        if (st->Failed)
            return;

        // Döngü: ilk noktayı sona ekle
        std::vector<Point> points = input;
        if (params.Loop && points.size() > 2)
            points.push_back(points.front());
        const size_t n = points.size();
        const glm::vec3 camPos = Renderer3D::GetCamera().Position;

        // Dokunun uzunluk boyunca birikimi
        std::vector<float> dist(n, 0.0f);
        for (size_t i = 1; i < n; ++i)
            dist[i] = dist[i - 1] + glm::distance(points[i].Position, points[i - 1].Position);
        const float total = std::max(dist.back(), 1e-5f);

        st->Vertices.clear();
        st->Vertices.reserve((n - 1) * 6);
        std::vector<glm::vec3> left(n), right(n);
        for (size_t i = 0; i < n; ++i)
        {
            glm::vec3 prev = points[i == 0 ? 0 : i - 1].Position;
            glm::vec3 next = points[i + 1 < n ? i + 1 : n - 1].Position;
            if (params.Loop && n > 2 && (i == 0 || i == n - 1))
            {
                prev = points[n - 2].Position;
                next = points[1].Position;
            }
            glm::vec3 tangent = next - prev;
            if (glm::dot(tangent, tangent) < 1e-12f)
                tangent = glm::vec3(1, 0, 0);
            tangent = glm::normalize(tangent);
            glm::vec3 normal = params.AlignToView ? glm::normalize(camPos - points[i].Position + glm::vec3(1e-6f)) : glm::normalize(params.ViewNormal);
            glm::vec3 side = glm::cross(tangent, normal);
            if (glm::dot(side, side) < 1e-10f)
                side = glm::cross(tangent, glm::vec3(0, 1, 0.001f));
            side = glm::normalize(side) * (points[i].Width * 0.5f);
            left[i] = points[i].Position - side;
            right[i] = points[i].Position + side;
        }
        for (size_t i = 0; i + 1 < n; ++i)
        {
            float u0 = params.TileTexture ? dist[i] / std::max(points[i].Width, 1e-4f) : dist[i] / total;
            float u1 = params.TileTexture ? dist[i + 1] / std::max(points[i + 1].Width, 1e-4f) : dist[i + 1] / total;
            RibbonVertex a{ left[i], { u0, 0.0f }, points[i].Color };
            RibbonVertex b{ right[i], { u0, 1.0f }, points[i].Color };
            RibbonVertex c{ left[i + 1], { u1, 0.0f }, points[i + 1].Color };
            RibbonVertex d{ right[i + 1], { u1, 1.0f }, points[i + 1].Color };
            st->Vertices.push_back(a); st->Vertices.push_back(b); st->Vertices.push_back(c);
            st->Vertices.push_back(b); st->Vertices.push_back(d); st->Vertices.push_back(c);
        }
        if (st->Vertices.empty())
            return;

        glBindBuffer(GL_ARRAY_BUFFER, st->VBO);
        size_t bytes = st->Vertices.size() * sizeof(RibbonVertex);
        if (bytes > st->Capacity)
        {
            st->Capacity = std::max(bytes, st->Capacity * 2);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)st->Capacity, nullptr, GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, st->Vertices.data());

        st->RibbonShader->Bind();
        bool hasTex = params.Texture && params.Texture->GetRendererID();
        st->RibbonShader->SetInt("u_HasTexture", hasTex ? 1 : 0);
        st->RibbonShader->SetInt("u_Additive", params.Additive ? 1 : 0);
        st->RibbonShader->SetFloat("u_Emissive", params.Emissive);
        if (hasTex)
            params.Texture->Bind(0);
        st->RibbonShader->SetInt("u_Texture", 0);

        GLboolean blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glEnable(GL_BLEND);
        if (params.Additive) glBlendFunc(GL_ONE, GL_ONE);
        else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glBindVertexArray(st->VAO);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)st->Vertices.size());
        glBindVertexArray(0);
        glDepthMask(depthMask);
        if (!blend) glDisable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        auto& stats = Renderer::GetStats();
        stats.DrawCalls++;
        stats.Triangles += (uint32_t)(st->Vertices.size() / 3);
        stats.Vertices += (uint32_t)st->Vertices.size();
    }

    void RibbonRenderer::Shutdown()
    {
        if (!s_State)
            return;
        if (s_State->VAO) glDeleteVertexArrays(1, &s_State->VAO);
        if (s_State->VBO) glDeleteBuffers(1, &s_State->VBO);
        delete s_State;
        s_State = nullptr;
    }
}
