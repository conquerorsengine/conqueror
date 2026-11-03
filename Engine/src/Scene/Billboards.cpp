#include "Billboards.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/GraphicsAPIs/OpenGL/GLProfile.h"

#include <glad/glad.h>
#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct BillboardVertex
        {
            glm::vec3 Position;
            glm::vec2 UV;
            glm::vec4 Color;
            float Shape;
        };

        // Örtülme sorgusu: görünüm (çizim tamponu) başına, bir önceki karenin sonucu okunur (bekleme yok)
        // OpenGL ES 3.x'te örnek sayan GL_SAMPLES_PASSED yok, yalnızca var/yok bildiren GL_ANY_SAMPLES_PASSED var:
        // ES'de test dörtgeni kEsGrid x kEsGrid parçaya bölünür, görünürlük = görünen parça oranı
        constexpr int kEsGrid = 4;
        constexpr int kMaxQueries = kEsGrid * kEsGrid;

        struct OcclusionQuery
        {
            GLuint Query[kMaxQueries] = {};
            int Count = 0;             // masaüstü 1 (örnek sayımı), ES kMaxQueries
            bool Pending = false;
            float Expected = 1.0f;     // test dörtgeninin beklenen örnek sayısı
            float Visibility = 1.0f;
            uint64_t LastFrame = 0;
        };

        struct State
        {
            std::shared_ptr<Shader> BillboardShader;
            GLuint Vao = 0, Vbo = 0;
            size_t Capacity = 0;
            std::map<std::pair<uint32_t, GLint>, OcclusionQuery> Queries;
            uint64_t Frame = 0;
            std::shared_ptr<Texture2D> White;
        };
        State* s_State = nullptr;

        State& S()
        {
            if (!s_State)
                s_State = new State();
            return *s_State;
        }

        bool EnsureResources()
        {
            State& st = S();
            if (!st.BillboardShader)
                st.BillboardShader = Shader::Create("Engine/src/Shaders/3D/Billboard.cqsh");
            if (!st.Vao)
            {
                glCreateVertexArrays(1, &st.Vao);
                glCreateBuffers(1, &st.Vbo);
                glVertexArrayVertexBuffer(st.Vao, 0, st.Vbo, 0, sizeof(BillboardVertex));
                const GLuint sizes[4] = { 3, 2, 4, 1 };
                const GLuint offsets[4] = { offsetof(BillboardVertex, Position), offsetof(BillboardVertex, UV), offsetof(BillboardVertex, Color), offsetof(BillboardVertex, Shape) };
                for (GLuint i = 0; i < 4; ++i)
                {
                    glEnableVertexArrayAttrib(st.Vao, i);
                    glVertexArrayAttribFormat(st.Vao, i, (GLint)sizes[i], GL_FLOAT, GL_FALSE, offsets[i]);
                    glVertexArrayAttribBinding(st.Vao, i, 0);
                }
            }
            return st.BillboardShader && st.BillboardShader->GetRendererID();
        }

        void Upload(const std::vector<BillboardVertex>& verts)
        {
            State& st = S();
            const size_t bytes = verts.size() * sizeof(BillboardVertex);
            if (bytes > st.Capacity)
            {
                st.Capacity = std::max(bytes, st.Capacity * 2);
                glNamedBufferData(st.Vbo, (GLsizeiptr)st.Capacity, nullptr, GL_DYNAMIC_DRAW);
            }
            glNamedBufferSubData(st.Vbo, 0, (GLsizeiptr)bytes, verts.data());
        }

        void Draw(const std::vector<BillboardVertex>& verts)
        {
            if (verts.empty())
                return;
            Upload(verts);
            glBindVertexArray(S().Vao);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
            glBindVertexArray(0);
        }

        void PushQuad(std::vector<BillboardVertex>& out, const glm::vec3& center, const glm::vec3& right, const glm::vec3& up, const glm::vec4& color, float shape,
                      const glm::vec2& uvMin = glm::vec2(0.0f), const glm::vec2& uvMax = glm::vec2(1.0f))
        {
            const glm::vec3 p[4] = { center - right - up, center + right - up, center + right + up, center - right + up };
            const glm::vec2 uv[4] = { uvMin, { uvMax.x, uvMin.y }, uvMax, { uvMin.x, uvMax.y } };
            const int idx[6] = { 0, 1, 2, 2, 3, 0 };
            for (int i : idx)
                out.push_back({ p[i], uv[i], color, shape });
        }

        std::shared_ptr<Texture2D> LoadTexture(const std::string& path, std::string& loaded, std::shared_ptr<Texture2D>& cache)
        {
            if (path != loaded)
            {
                loaded = path;
                cache = path.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(path));
                if (cache && !cache->GetRendererID())
                    cache = nullptr;
            }
            return cache;
        }

        // Sahnenin ışıkları: billboard'ları kabaca aydınlatmak için ortam + güneş
        void SunAndAmbient(Scene& scene, glm::vec3& sunDir, glm::vec3& sunColor, glm::vec3& ambient)
        {
            sunDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
            sunColor = glm::vec3(0.5f);
            ambient = scene.GetAmbientColor() * scene.GetAmbientIntensity();
            Entity sun = scene.GetSunSourceEntity();
            if (sun && sun.HasComponent<DirectionalLightComponent>() && scene.IsEntityActive(sun))
            {
                const auto& d = sun.GetComponent<DirectionalLightComponent>();
                if (glm::length(d.Direction) > 1e-5f)
                    sunDir = glm::normalize(d.Direction);
                sunColor = d.Color * d.Intensity;
            }
        }

        std::shared_ptr<Texture2D> WhiteTexture()
        {
            State& st = S();
            if (!st.White)
            {
                st.White = Texture2D::Create(1, 1);
                uint32_t px = 0xffffffff;
                st.White->SetData(&px, sizeof(px));
            }
            return st.White;
        }

        glm::vec3 CameraRight(const RenderCamera& c) { return glm::vec3(glm::inverse(c.View)[0]); }
        glm::vec3 CameraUp(const RenderCamera& c) { return glm::vec3(glm::inverse(c.View)[1]); }
        glm::vec3 CameraForward(const RenderCamera& c) { return -glm::vec3(glm::inverse(c.View)[2]); }

        struct DepthAndBlend
        {
            GLboolean DepthTest, Blend, Cull, DepthMask;
            DepthAndBlend()
            {
                DepthTest = glIsEnabled(GL_DEPTH_TEST);
                Blend = glIsEnabled(GL_BLEND);
                Cull = glIsEnabled(GL_CULL_FACE);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &DepthMask);
            }
            ~DepthAndBlend()
            {
                if (DepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
                if (Blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
                if (Cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
                glDepthMask(DepthMask);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                for (GLuint i = 1; i < 4; ++i)
                    glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            }
        };
    }

    namespace Billboards
    {
        void RenderOpaque(Scene& scene, const RenderCamera& camera)
        {
            auto view = scene.m_Registry.view<BillboardRendererComponent, TransformComponent>();
            if (view.begin() == view.end() || !EnsureResources())
                return;
            glm::vec3 sunDir, sunColor, ambient;
            SunAndAmbient(scene, sunDir, sunColor, ambient);
            const glm::vec3 camRight = CameraRight(camera), camUp = CameraUp(camera), camFwd = CameraForward(camera);

            // Dokuya göre gruplanır (doku başına tek çizim)
            std::unordered_map<Texture2D*, std::pair<std::shared_ptr<Texture2D>, std::vector<BillboardVertex>>> batches;
            for (auto e : view)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                auto& b = view.get<BillboardRendererComponent>(e);
                const glm::mat4& world = view.get<TransformComponent>(e).WorldTransform;
                auto tex = LoadTexture(b.Texture, b.LoadedTexture, b.TextureRef);
                const glm::vec3 pos = glm::vec3(world[3]);
                const glm::vec2 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])));
                const glm::vec2 half = b.Size * scale * 0.5f;
                glm::vec3 right, up;
                if (b.Alignment == BillboardAlignment::VerticalAxis)
                {
                    // Dünya yukarı ekseni sabit (ağaçlar eğilmez), kameraya yatayda döner
                    up = glm::vec3(0, 1, 0);
                    glm::vec3 toCam = camera.Orthographic ? -camFwd : camera.Position - pos;
                    toCam.y = 0.0f;
                    right = glm::length(toCam) > 1e-5f ? glm::normalize(glm::cross(up, glm::normalize(toCam))) : camRight;
                }
                else
                {
                    right = camRight;
                    up = camUp;
                }
                glm::vec3 center = pos + (b.PivotAtBottom ? up * half.y : glm::vec3(0.0f));
                glm::vec4 color = b.Color;
                if (b.Lit)
                {
                    // Yarım Lambert: kameraya dönük yüzey normali ile güneş
                    glm::vec3 n = glm::normalize(glm::cross(right, up));
                    if (glm::dot(n, camera.Position - center) < 0.0f) n = -n;
                    float wrap = 0.5f + 0.5f * glm::dot(n, -sunDir);
                    color = glm::vec4(glm::vec3(color) * (ambient + sunColor * wrap), color.a);
                }
                auto& batch = batches[tex.get()];
                batch.first = tex;
                PushQuad(batch.second, center, right * half.x, up * half.y, color, 0.0f);
            }
            if (batches.empty())
                return;
            DepthAndBlend restore;
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            for (GLuint i = 1; i < 4; ++i)
                glColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            Shader& sh = *S().BillboardShader;
            sh.Bind();
            sh.SetMat4("u_ViewProjection", camera.Projection * camera.View);
            sh.SetInt("u_Texture", 0);
            for (auto& [key, batch] : batches)
            {
                (void)key;
                if (batch.first)
                    batch.first->Bind(0);
                else
                    WhiteTexture()->Bind(0);
                float cutoff = 0.5f;
                for (auto e : view)
                    if (view.get<BillboardRendererComponent>(e).TextureRef == batch.first)
                    {
                        cutoff = view.get<BillboardRendererComponent>(e).AlphaCutoff;
                        break;
                    }
                sh.SetFloat("u_AlphaCutoff", cutoff);
                Draw(batch.second);
            }
        }

        void RenderEffects(Scene& scene, const RenderCamera& camera)
        {
            auto halos = scene.m_Registry.view<HaloComponent, TransformComponent>();
            auto flares = scene.m_Registry.view<LensFlareComponent, TransformComponent>();
            if ((halos.begin() == halos.end() && flares.begin() == flares.end()) || !EnsureResources())
                return;
            State& st = S();
            ++st.Frame;
            GLint fbo = 0, vp[4], samples = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
            glGetIntegerv(GL_VIEWPORT, vp);
            glGetIntegerv(GL_SAMPLES, &samples);
            samples = std::max(samples, 1);
            const glm::mat4 viewProj = camera.Projection * camera.View;
            const glm::vec3 camRight = CameraRight(camera), camUp = CameraUp(camera), camFwd = CameraForward(camera);
            const float aspect = vp[3] > 0 ? (float)vp[2] / (float)vp[3] : 1.0f;

            DepthAndBlend restore;
            glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE);
            for (GLuint i = 1; i < 4; ++i)
                glColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            Shader& sh = *st.BillboardShader;
            sh.Bind();

            // ---- Halolar: derinlik testli dünya uzayı parıltı
            {
                std::vector<BillboardVertex> verts;
                for (auto e : halos)
                {
                    if (!scene.IsEntityActive(e))
                        continue;
                    const auto& h = halos.get<HaloComponent>(e);
                    const glm::vec3 pos = glm::vec3(halos.get<TransformComponent>(e).WorldTransform[3]);
                    PushQuad(verts, pos, camRight * h.Size, camUp * h.Size, glm::vec4(h.Color, std::max(h.Intensity, 0.0f)), 1.0f);
                }
                if (!verts.empty())
                {
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LEQUAL);
                    glEnable(GL_BLEND);
                    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
                    sh.SetMat4("u_ViewProjection", viewProj);
                    Draw(verts);
                }
            }

            // ---- Lens parlamaları
            struct FlareDraw { LensFlareComponent* F; glm::vec2 Ndc; glm::vec3 Color; float Fade; };
            std::vector<FlareDraw> draws;
            std::vector<std::pair<OcclusionQuery*, std::vector<BillboardVertex>>> tests;
            for (auto e : flares)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                auto& f = flares.get<LensFlareComponent>(e);
                const glm::mat4& world = flares.get<TransformComponent>(e).WorldTransform;
                glm::vec3 color = f.Color * std::max(f.Intensity, 0.0f);
                glm::vec3 pos = glm::vec3(world[3]);
                float testRadius = std::max(f.OcclusionRadius, 0.001f);
                Entity ent{ e, &scene };
                if (ent.HasComponent<DirectionalLightComponent>())
                {
                    // Güneş: kameradan ışık yönünün tersine, uzak düzlemin hemen önü
                    const auto& d = ent.GetComponent<DirectionalLightComponent>();
                    glm::vec3 dir = glm::length(d.Direction) > 1e-5f ? glm::normalize(d.Direction) : glm::vec3(0, -1, 0);
                    const float far = camera.Orthographic ? 500.0f : camera.FarClip * 0.9f;
                    pos = camera.Position - dir * far;
                    testRadius = far * std::max(f.OcclusionRadius, 0.001f) * 0.05f;
                    if (f.UseLightColor) color *= d.Color;
                }
                else if (f.UseLightColor && ent.HasComponent<PointLightComponent>())
                    color *= ent.GetComponent<PointLightComponent>().Color;
                else if (f.UseLightColor && ent.HasComponent<SpotLightComponent>())
                    color *= ent.GetComponent<SpotLightComponent>().Color;
                glm::vec4 clip = viewProj * glm::vec4(pos, 1.0f);
                if (clip.w <= 0.0f)
                {
                    f.Visibility = 0.0f;
                    continue;
                }
                glm::vec2 ndc = glm::vec2(clip) / clip.w;
                // Ekran kenarına yaklaşınca söner
                float edge = std::max(std::fabs(ndc.x), std::fabs(ndc.y));
                float fade = 1.0f - std::clamp((edge - 0.85f) / 0.3f, 0.0f, 1.0f);
                if (f.MaxDistance > 0.0f)
                    fade *= 1.0f - std::clamp(glm::length(pos - camera.Position) / f.MaxDistance - 0.8f, 0.0f, 0.2f) / 0.2f;
                if (fade <= 0.0f)
                {
                    f.Visibility = 0.0f;
                    continue;
                }

                float visibility = 1.0f;
                if (f.UseOcclusion)
                {
                    auto& q = st.Queries[{ (uint32_t)e, fbo }];
                    if (!q.Count)
                    {
                        q.Count = GLProfile::Current().ES ? kMaxQueries : 1;
                        glGenQueries(q.Count, q.Query);
                    }
                    if (q.Pending)
                    {
                        // Sonuçlar sırayla hazır olur: son sorgu hazırsa hepsi hazır
                        GLuint available = 0;
                        glGetQueryObjectuiv(q.Query[q.Count - 1], GL_QUERY_RESULT_AVAILABLE, &available);
                        if (available)
                        {
                            float measured = 0.0f;
                            if (q.Count == 1)
                            {
                                GLuint passed = 0;
                                glGetQueryObjectuiv(q.Query[0], GL_QUERY_RESULT, &passed);
                                measured = std::clamp((float)passed / std::max(q.Expected, 1.0f), 0.0f, 1.0f);
                            }
                            else
                            {
                                GLuint visibleParts = 0;
                                for (int i = 0; i < q.Count; ++i)
                                {
                                    GLuint any = 0;
                                    glGetQueryObjectuiv(q.Query[i], GL_QUERY_RESULT, &any);
                                    visibleParts += any ? 1u : 0u;
                                }
                                measured = (float)visibleParts / (float)q.Count;
                            }
                            // Yumuşak geçiş (ani kesilme olmasın)
                            q.Visibility += (measured - q.Visibility) * 0.5f;
                            q.Pending = false;
                        }
                    }
                    q.LastFrame = st.Frame;
                    visibility = q.Visibility;
                    if (!q.Pending)
                    {
                        // Test dörtgeni: ışığın konumunda kameraya dönük, derinlik testli
                        std::vector<BillboardVertex> quad;
                        if (q.Count == 1)
                            PushQuad(quad, pos, camRight * testRadius, camUp * testRadius, glm::vec4(1.0f), 6.0f);
                        else
                        {
                            const float part = testRadius / (float)kEsGrid;
                            for (int y = 0; y < kEsGrid; ++y)
                                for (int x = 0; x < kEsGrid; ++x)
                                {
                                    const glm::vec3 c = pos + camRight * (testRadius * (-1.0f + (2.0f * (float)x + 1.0f) / (float)kEsGrid)) +
                                                        camUp * (testRadius * (-1.0f + (2.0f * (float)y + 1.0f) / (float)kEsGrid));
                                    PushQuad(quad, c, camRight * part, camUp * part, glm::vec4(1.0f), 6.0f);
                                }
                        }
                        // Beklenen örnek sayısı: dörtgenin ekrandaki piksel alanı
                        glm::vec4 a = viewProj * glm::vec4(pos - camRight * testRadius - camUp * testRadius, 1.0f);
                        glm::vec4 b = viewProj * glm::vec4(pos + camRight * testRadius + camUp * testRadius, 1.0f);
                        glm::vec2 na = glm::vec2(a) / std::max(a.w, 1e-6f), nb = glm::vec2(b) / std::max(b.w, 1e-6f);
                        float px = std::fabs(nb.x - na.x) * 0.5f * (float)vp[2], py = std::fabs(nb.y - na.y) * 0.5f * (float)vp[3];
                        q.Expected = std::max(px * py, 1.0f) * (float)samples;
                        tests.emplace_back(&q, std::move(quad));
                    }
                }
                f.Visibility = visibility * fade;
                if (f.Visibility > 0.001f)
                    draws.push_back({ &f, ndc, color, f.Visibility });
            }

            // Örtülme testleri: renk yazmadan derinlik testli örnek sayımı (sonuç bir sonraki karede)
            if (!tests.empty())
            {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDisable(GL_BLEND);
                sh.SetMat4("u_ViewProjection", viewProj);
                for (auto& [q, quad] : tests)
                {
                    if (q->Count == 1)
                    {
                        glBeginQuery(GL_SAMPLES_PASSED, q->Query[0]);
                        Draw(quad);
                        glEndQuery(GL_SAMPLES_PASSED);
                    }
                    else
                    {
                        Upload(quad);
                        glBindVertexArray(st.Vao);
                        for (int i = 0; i < q->Count; ++i)
                        {
                            glBeginQuery(GL_ANY_SAMPLES_PASSED, q->Query[i]);
                            glDrawArrays(GL_TRIANGLES, i * 6, 6);
                            glEndQuery(GL_ANY_SAMPLES_PASSED);
                        }
                        glBindVertexArray(0);
                    }
                    q->Pending = true;
                }
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                for (GLuint i = 1; i < 4; ++i)
                    glColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            }

            // Parlama öğeleri: ekran uzayı (NDC), derinlik testi yok
            if (!draws.empty())
            {
                std::vector<BillboardVertex> verts;
                const glm::vec2 aspectFix(1.0f / std::max(aspect, 0.01f), 1.0f);
                auto element = [&](const glm::vec2& c, float size, const glm::vec3& col, float intensity, float shape, float stretchX = 1.0f) {
                    if (intensity <= 0.0f || size <= 0.0f)
                        return;
                    glm::vec2 h = glm::vec2(size * stretchX, size) * aspectFix * 2.0f;
                    PushQuad(verts, glm::vec3(c, 0.0f), glm::vec3(h.x, 0, 0), glm::vec3(0, h.y, 0), glm::vec4(col, intensity), shape);
                };
                for (auto& d : draws)
                {
                    const LensFlareComponent& f = *d.F;
                    const float s = std::max(f.Scale, 0.0f);
                    const glm::vec3 col = d.Color * d.Fade;
                    // Lens Flare varlığı: öğeler dosyadan
                    std::vector<LensFlareElement> elements;
                    if (!f.Flare.empty() && LensFlareAssets::Load(f.Flare, elements))
                    {
                        for (const auto& el : elements)
                            for (int i = 0; i < std::clamp(el.Count, 1, 32); ++i)
                            {
                                const float t = el.Position + (float)i * el.Spacing;
                                const glm::vec2 p = d.Ndc * (1.0f - 2.0f * t);
                                const float shape = el.Shape == LensFlareShape::Glow ? 1.0f : el.Shape == LensFlareShape::Ring ? 2.0f
                                                  : el.Shape == LensFlareShape::Hexagon ? 3.0f : el.Shape == LensFlareShape::Circle ? 4.0f : 5.0f;
                                element(p, el.Size * s, col * el.Color, el.Intensity, shape, std::max(el.Stretch, 0.01f));
                            }
                        continue;
                    }
                    element(d.Ndc, f.GlowSize * s, col, f.GlowIntensity, 1.0f);
                    element(d.Ndc, f.RingSize * s, col, f.RingIntensity, 2.0f);
                    element(d.Ndc, f.GlowSize * s, col, f.StreakIntensity, 5.0f, std::max(f.StreakLength, 0.0f) / std::max(f.GlowSize, 1e-3f) * 1.5f);
                    // Hayaletler: ışıktan ekran merkezinin karşı tarafına doğru, renkleri hafifçe kayar
                    for (int i = 0; i < std::clamp(f.GhostCount, 0, 16); ++i)
                    {
                        const float t = 0.5f + (float)i * f.GhostSpacing;
                        const glm::vec2 p = d.Ndc * (1.0f - 2.0f * t);
                        const float k = (float)i / std::max(1.0f, (float)f.GhostCount - 1.0f);
                        const glm::vec3 tint = col * glm::mix(glm::vec3(1.0f, 0.75f, 0.45f), glm::vec3(0.45f, 0.75f, 1.0f), k);
                        const float size = f.GhostSize * s * (0.6f + 0.8f * std::fmod((float)i * 0.618f, 1.0f));
                        element(p, size, tint, f.GhostIntensity, f.HexagonGhosts ? 3.0f : 4.0f);
                    }
                }
                glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
                sh.SetMat4("u_ViewProjection", glm::mat4(1.0f));
                Draw(verts);
            }

            // Uzun süredir kullanılmayan sorgular (silinmiş varlık / kapanmış görünüm)
            for (auto it = st.Queries.begin(); it != st.Queries.end();)
            {
                if (st.Frame - it->second.LastFrame > 600)
                {
                    if (it->second.Count)
                        glDeleteQueries(it->second.Count, it->second.Query);
                    it = st.Queries.erase(it);
                }
                else
                    ++it;
            }
        }

        bool GetLocalBounds(Scene& scene, uint32_t entity, glm::vec3& outMin, glm::vec3& outMax)
        {
            entt::entity e = (entt::entity)entity;
            auto& reg = scene.m_Registry;
            if (auto* b = reg.try_get<BillboardRendererComponent>(e))
            {
                const float r = std::max(b->Size.x, b->Size.y) * 0.5f;
                outMin = glm::vec3(-r, b->PivotAtBottom ? 0.0f : -b->Size.y * 0.5f, -r);
                outMax = glm::vec3(r, b->PivotAtBottom ? b->Size.y : b->Size.y * 0.5f, r);
                return true;
            }
            if (auto* h = reg.try_get<HaloComponent>(e))
            {
                outMin = glm::vec3(-h->Size);
                outMax = glm::vec3(h->Size);
                return true;
            }
            return false;
        }

        void Shutdown()
        {
            if (!s_State)
                return;
            for (auto& [key, q] : s_State->Queries)
                if (q.Count)
                    glDeleteQueries(q.Count, q.Query);
            if (s_State->Vao) glDeleteVertexArrays(1, &s_State->Vao);
            if (s_State->Vbo) glDeleteBuffers(1, &s_State->Vbo);
            delete s_State;
            s_State = nullptr;
        }
    }

    namespace LensFlareAssets
    {
        std::vector<LensFlareElement> DefaultElements()
        {
            LensFlareComponent f;   // yerleşik görünümün aynısı
            std::vector<LensFlareElement> e;
            e.push_back({ LensFlareShape::Glow, glm::vec3(1.0f), f.GlowSize, f.GlowIntensity, 0.0f, 1, 0.0f, 1.0f });
            e.push_back({ LensFlareShape::Ring, glm::vec3(1.0f), f.RingSize, f.RingIntensity, 0.0f, 1, 0.0f, 1.0f });
            e.push_back({ LensFlareShape::Streak, glm::vec3(1.0f), f.GlowSize, f.StreakIntensity, 0.0f, 1, 0.0f, f.StreakLength / f.GlowSize * 1.5f });
            e.push_back({ LensFlareShape::Hexagon, glm::vec3(1.0f, 0.75f, 0.45f), f.GhostSize, f.GhostIntensity, 0.5f, 2, f.GhostSpacing, 1.0f });
            e.push_back({ LensFlareShape::Hexagon, glm::vec3(0.45f, 0.75f, 1.0f), f.GhostSize * 1.3f, f.GhostIntensity, 0.5f + 2.0f * f.GhostSpacing, 2, f.GhostSpacing, 1.0f });
            return e;
        }

        bool Load(const std::string& path, std::vector<LensFlareElement>& elements)
        {
            struct Cached { std::filesystem::file_time_type Time; std::vector<LensFlareElement> Elements; };
            static std::unordered_map<std::string, Cached> cache;
            const std::string file = SceneSerializer::ResolveAssetPath(path);
            std::error_code ec;
            const auto t = std::filesystem::last_write_time(file, ec);
            if (ec)
                return false;
            auto it = cache.find(file);
            if (it == cache.end() || it->second.Time != t)
            {
                try
                {
                    YAML::Node n = YAML::LoadFile(file)["LensFlare"];
                    if (!n || !n["Elements"])
                        return false;
                    Cached c{ t, {} };
                    for (const auto& x : n["Elements"])
                    {
                        LensFlareElement el;
                        if (x["Shape"]) el.Shape = (LensFlareShape)std::clamp(x["Shape"].as<int>(), 0, 4);
                        if (auto col = x["Color"]; col && col.IsSequence() && col.size() == 3) el.Color = { col[0].as<float>(), col[1].as<float>(), col[2].as<float>() };
                        if (x["Size"]) el.Size = x["Size"].as<float>();
                        if (x["Intensity"]) el.Intensity = x["Intensity"].as<float>();
                        if (x["Position"]) el.Position = x["Position"].as<float>();
                        if (x["Count"]) el.Count = x["Count"].as<int>();
                        if (x["Spacing"]) el.Spacing = x["Spacing"].as<float>();
                        if (x["Stretch"]) el.Stretch = x["Stretch"].as<float>();
                        c.Elements.push_back(el);
                    }
                    it = cache.insert_or_assign(file, std::move(c)).first;
                }
                catch (...) { return false; }
            }
            elements = it->second.Elements;
            return true;
        }

        bool Save(const std::string& path, const std::vector<LensFlareElement>& elements)
        {
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "LensFlare" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Elements" << YAML::Value << YAML::BeginSeq;
            for (const auto& el : elements)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Shape" << YAML::Value << (int)el.Shape;
                out << YAML::Key << "Color" << YAML::Value << YAML::Flow << YAML::BeginSeq << el.Color.r << el.Color.g << el.Color.b << YAML::EndSeq;
                out << YAML::Key << "Size" << YAML::Value << el.Size;
                out << YAML::Key << "Intensity" << YAML::Value << el.Intensity;
                out << YAML::Key << "Position" << YAML::Value << el.Position;
                out << YAML::Key << "Count" << YAML::Value << el.Count;
                out << YAML::Key << "Spacing" << YAML::Value << el.Spacing;
                out << YAML::Key << "Stretch" << YAML::Value << el.Stretch;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
            std::ofstream f(SceneSerializer::ResolveAssetPath(path), std::ios::trunc);
            if (!f)
                return false;
            f << out.c_str() << "\n";
            return true;
        }
    }

    void RegisterBillboardComponents()
    {
        using B = BillboardRendererComponent;
        ComponentDef<B>("Billboard Renderer", "BillboardRendererComponent", "Rendering", ICON_FA_TREE)
            .Describe("A textured quad that always faces the camera: distant trees, grass, signs, impostors.")
            .Field("Texture", "Texture", &B::Texture, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga")
            .Field("Color", "Color", &B::Color, FieldKind::Color4)
            .Field("Size", "Size", &B::Size).Speed(0.01f).Tooltip("Width and height in metres (multiplied by the scale).")
            .Field("Alignment", "Alignment", &B::Alignment).Options({ "Face Camera", "Vertical Axis" })
                .Tooltip("Vertical Axis keeps the quad upright (trees); Face Camera turns it fully towards the camera.")
            .Field("PivotAtBottom", "Pivot At Bottom", &B::PivotAtBottom)
            .Field("AlphaCutoff", "Alpha Cutoff", &B::AlphaCutoff).Range(0.0f, 1.0f, 0.01f)
            .Field("Lit", "Lit", &B::Lit).Tooltip("Tint by the ambient light and the sun.");

        using H = HaloComponent;
        ComponentDef<H>("Halo", "HaloComponent", "Rendering", ICON_FA_SUN)
            .Describe("A soft glow around the object (lamps, magic orbs, street lights).")
            .Field("Color", "Color", &H::Color, FieldKind::Color3)
            .Field("Size", "Size", &H::Size).Range(0.0f, 1000.0f, 0.01f)
            .Field("Intensity", "Intensity", &H::Intensity).Range(0.0f, 100.0f, 0.01f);

        using F = LensFlareComponent;
        ComponentDef<F>("Lens Flare", "LensFlareComponent", "Rendering", ICON_FA_STAR)
            .Describe("Camera lens flare from this object or light (on a Directional Light it follows the sun). Fades when hidden behind geometry or leaving the screen.")
            .Field("Flare", "Lens Flare Data", &F::Flare, FieldKind::Asset).Filter(".cqflare")
                .Tooltip("Lens Flare asset (.cqflare): its elements replace the Elements settings below.")
            .Field("Color", "Color", &F::Color, FieldKind::Color3)
            .Field("Intensity", "Intensity", &F::Intensity).Range(0.0f, 100.0f, 0.01f)
            .Field("Scale", "Scale", &F::Scale).Range(0.0f, 10.0f, 0.01f)
            .Field("UseLightColor", "Use Light Color", &F::UseLightColor)
            .Field("UseOcclusion", "Occlusion", &F::UseOcclusion).Header("Occlusion")
            .Field("OcclusionRadius", "Occlusion Radius", &F::OcclusionRadius).Range(0.001f, 100.0f, 0.01f)
            .Field("MaxDistance", "Max Distance", &F::MaxDistance).Range(0.0f, 100000.0f, 1.0f).Tooltip("0 = no distance fade.")
            .Field("GlowSize", "Glow Size", &F::GlowSize).Range(0.0f, 4.0f, 0.005f).Header("Elements")
            .Field("GlowIntensity", "Glow Intensity", &F::GlowIntensity).Range(0.0f, 20.0f, 0.01f)
            .Field("RingSize", "Ring Size", &F::RingSize).Range(0.0f, 4.0f, 0.005f)
            .Field("RingIntensity", "Ring Intensity", &F::RingIntensity).Range(0.0f, 20.0f, 0.01f)
            .Field("GhostCount", "Ghost Count", &F::GhostCount).Range(0.0f, 16.0f, 1.0f)
            .Field("GhostSpacing", "Ghost Spacing", &F::GhostSpacing).Range(0.0f, 2.0f, 0.005f)
            .Field("GhostSize", "Ghost Size", &F::GhostSize).Range(0.0f, 2.0f, 0.005f)
            .Field("GhostIntensity", "Ghost Intensity", &F::GhostIntensity).Range(0.0f, 20.0f, 0.01f)
            .Field("HexagonGhosts", "Hexagon Ghosts", &F::HexagonGhosts)
            .Field("StreakLength", "Streak Length", &F::StreakLength).Range(0.0f, 4.0f, 0.01f)
            .Field("StreakIntensity", "Streak Intensity", &F::StreakIntensity).Range(0.0f, 20.0f, 0.01f)
            .Field("Visibility", "Visibility", &F::Visibility).ReadOnly();
        // Varlık kullanılırken yerleşik öğe ayarları gizli
        if (ComponentInfo* info = ComponentRegistry::Find("LensFlareComponent"))
        {
            bool elements = false;
            for (auto& f : info->Fields)
            {
                if (f.Name == "GlowSize") elements = true;
                if (f.Name == "Visibility") elements = false;
                if (elements)
                {
                    auto old = f.VisibleIf;
                    f.VisibleIf = [old](const void* c) { return static_cast<const F*>(c)->Flare.empty() && (!old || old(c)); };
                }
            }
        }
    }
}
