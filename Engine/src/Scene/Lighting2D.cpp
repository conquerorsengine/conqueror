#include "Lighting2D.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/PhysicsComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/Texture.h"

#include <glad/glad.h>
#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Conqueror
{
    namespace
    {
        // Görünüm başına ışık hedefi (oyun ve editör görünümü aynı karede farklı boyutta çizer)
        struct LightTarget
        {
            GLuint Fbo = 0, Tex = 0, Rbo = 0;
            int W = 0, H = 0;
            uint64_t LastUse = 0;
        };

        struct State
        {
            std::shared_ptr<Shader> LightShader, StencilShader;
            GLuint Vao = 0, Vbo = 0;
            size_t VboCapacity = 0;
            std::vector<LightTarget> Targets;
            uint64_t Use = 0;
        };
        State* s_State = nullptr;

        State& S()
        {
            if (!s_State)
                s_State = new State();
            return *s_State;
        }

        constexpr float kPi = 3.14159265f;

        bool EnsureResources()
        {
            State& st = S();
            if (!st.LightShader)
                st.LightShader = Shader::Create("Engine/src/Shaders/2D/Light2D.cqsh");
            if (!st.StencilShader)
                st.StencilShader = Shader::Create("Engine/src/Shaders/2D/Stencil2D.cqsh");
            if (!st.Vao)
            {
                glCreateVertexArrays(1, &st.Vao);
                glCreateBuffers(1, &st.Vbo);
                glVertexArrayVertexBuffer(st.Vao, 0, st.Vbo, 0, sizeof(glm::vec4));
                glEnableVertexArrayAttrib(st.Vao, 0);
                glVertexArrayAttribFormat(st.Vao, 0, 4, GL_FLOAT, GL_FALSE, 0);
                glVertexArrayAttribBinding(st.Vao, 0, 0);
            }
            return st.LightShader && st.StencilShader && st.LightShader->GetRendererID() && st.StencilShader->GetRendererID();
        }

        void DrawTriangles(const std::vector<glm::vec4>& verts)
        {
            if (verts.empty())
                return;
            State& st = S();
            const size_t bytes = verts.size() * sizeof(glm::vec4);
            if (bytes > st.VboCapacity)
            {
                st.VboCapacity = std::max(bytes, st.VboCapacity * 2);
                glNamedBufferData(st.Vbo, (GLsizeiptr)st.VboCapacity, nullptr, GL_DYNAMIC_DRAW);
            }
            glNamedBufferSubData(st.Vbo, 0, (GLsizeiptr)bytes, verts.data());
            glBindVertexArray(st.Vao);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
            glBindVertexArray(0);
        }

        void PushQuad(std::vector<glm::vec4>& out, const glm::vec2 p[4], const glm::vec2 uv[4])
        {
            const int idx[6] = { 0, 1, 2, 2, 3, 0 };
            for (int i : idx)
                out.emplace_back(p[i], uv[i]);
        }

        void PushRect(std::vector<glm::vec4>& out, const glm::vec2& mn, const glm::vec2& mx)
        {
            const glm::vec2 p[4] = { mn, { mx.x, mn.y }, mx, { mn.x, mx.y } };
            const glm::vec2 uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            PushQuad(out, p, uv);
        }

        LightTarget* AcquireTarget(int w, int h)
        {
            State& st = S();
            ++st.Use;
            for (auto& t : st.Targets)
                if (t.W == w && t.H == h)
                {
                    t.LastUse = st.Use;
                    return &t;
                }
            // Yeni hedef (en fazla 3; en eski kullanılan yeniden boyutlanır)
            LightTarget* t = nullptr;
            if (st.Targets.size() < 3)
                t = &st.Targets.emplace_back();
            else
            {
                t = &*std::min_element(st.Targets.begin(), st.Targets.end(), [](const LightTarget& a, const LightTarget& b) { return a.LastUse < b.LastUse; });
                glDeleteFramebuffers(1, &t->Fbo);
                glDeleteTextures(1, &t->Tex);
                glDeleteRenderbuffers(1, &t->Rbo);
                *t = LightTarget{};
            }
            glCreateTextures(GL_TEXTURE_2D, 1, &t->Tex);
            glTextureStorage2D(t->Tex, 1, GL_RGBA16F, w, h);
            glTextureParameteri(t->Tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(t->Tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(t->Tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(t->Tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glCreateRenderbuffers(1, &t->Rbo);
            glNamedRenderbufferStorage(t->Rbo, GL_DEPTH24_STENCIL8, w, h);
            glCreateFramebuffers(1, &t->Fbo);
            glNamedFramebufferTexture(t->Fbo, GL_COLOR_ATTACHMENT0, t->Tex, 0);
            glNamedFramebufferRenderbuffer(t->Fbo, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t->Rbo);
            t->W = w;
            t->H = h;
            t->LastUse = st.Use;
            return t;
        }

        std::shared_ptr<Texture2D> LoadSprite(const std::string& path, std::string& loaded, std::shared_ptr<Texture2D>& cache)
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

        glm::vec2 ToWorld(const glm::mat4& m, const glm::vec2& p) { return glm::vec2(m * glm::vec4(p, 0.0f, 1.0f)); }

        struct Caster
        {
            std::vector<glm::vec2> Poly;
            glm::vec2 Min{ 0.0f }, Max{ 0.0f };
            bool SelfShadows = false;
        };

        // Yerel şekil: özel çokgen > Box / Circle / Capsule / Polygon Collider 2D > sprite sınırı > birim kare
        std::vector<glm::vec2> CasterShape(Scene& scene, entt::entity e, const ShadowCaster2DComponent& sc)
        {
            auto& reg = scene.m_Registry;
            std::vector<glm::vec2> local;
            if (sc.Shape == ShadowCasterShape::Custom && sc.Points.size() >= 3)
                return sc.Points;
            if (auto* b = reg.try_get<BoxCollider2DComponent>(e))
            {
                glm::vec2 h = b->Size * 0.5f;
                return { b->Offset + glm::vec2(-h.x, -h.y), b->Offset + glm::vec2(h.x, -h.y), b->Offset + h, b->Offset + glm::vec2(-h.x, h.y) };
            }
            if (auto* c = reg.try_get<CircleCollider2DComponent>(e))
            {
                for (int i = 0; i < 20; ++i)
                {
                    float a = (float)i / 20.0f * 2.0f * kPi;
                    local.push_back(c->Offset + glm::vec2(std::cos(a), std::sin(a)) * c->Radius);
                }
                return local;
            }
            if (auto* cap = reg.try_get<CapsuleCollider2DComponent>(e))
            {
                const bool vertical = cap->Direction == 0;
                const float r = (vertical ? cap->Size.x : cap->Size.y) * 0.5f;
                const float half = std::max(0.0f, (vertical ? cap->Size.y : cap->Size.x) * 0.5f - r);
                const glm::vec2 axis = vertical ? glm::vec2(0, 1) : glm::vec2(1, 0);
                const float base = vertical ? 0.0f : -kPi * 0.5f;
                for (int i = 0; i <= 10; ++i)   // üst / sağ yarım daire
                {
                    float a = base + (float)i / 10.0f * kPi;
                    local.push_back(cap->Offset + axis * half + glm::vec2(std::cos(a), std::sin(a)) * r);
                }
                for (int i = 0; i <= 10; ++i)
                {
                    float a = base + kPi + (float)i / 10.0f * kPi;
                    local.push_back(cap->Offset - axis * half + glm::vec2(std::cos(a), std::sin(a)) * r);
                }
                return local;
            }
            if (auto* p = reg.try_get<PolygonCollider2DComponent>(e); p && p->Points.size() >= 3)
            {
                for (auto& q : p->Points)
                    local.push_back(q + p->Offset);
                return local;
            }
            if (auto* sr = reg.try_get<SpriteRendererComponent>(e))
            {
                glm::vec2 mn, mx;
                if (Renderer2D::GetSpriteLocalBounds(*sr, mn, mx))
                    return { mn, { mx.x, mn.y }, mx, { mn.x, mx.y } };
            }
            return { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
        }

        std::vector<Caster> GatherCasters(Scene& scene)
        {
            std::vector<Caster> out;
            auto view = scene.m_Registry.view<ShadowCaster2DComponent, TransformComponent>();
            for (auto e : view)
            {
                const auto& sc = view.get<ShadowCaster2DComponent>(e);
                if (!sc.CastsShadows || !scene.IsEntityActive(e))
                    continue;
                const glm::mat4& m = view.get<TransformComponent>(e).WorldTransform;
                Caster c;
                c.SelfShadows = sc.SelfShadows;
                for (auto& p : CasterShape(scene, e, sc))
                    c.Poly.push_back(ToWorld(m, p));
                if (c.Poly.size() < 3)
                    continue;
                c.Min = c.Max = c.Poly[0];
                for (auto& p : c.Poly)
                {
                    c.Min = glm::min(c.Min, p);
                    c.Max = glm::max(c.Max, p);
                }
                out.push_back(std::move(c));
            }
            return out;
        }

        // Işık geometrisi (dünya) + etki alanı sınırı
        struct LightDraw
        {
            const Light2DComponent* L = nullptr;
            glm::mat4 World{ 1.0f };
            glm::vec2 Center{ 0.0f }, Axis{ 0, 1 };
            glm::vec2 Min{ 0.0f }, Max{ 0.0f };
            std::vector<glm::vec4> Geometry;
            std::vector<glm::vec2> Poly;          // Freeform (dünya)
            std::shared_ptr<Texture2D> Cookie;
        };

        bool BuildLight(Light2DComponent& l, const glm::mat4& world, LightDraw& d)
        {
            d.L = &l;
            d.World = world;
            d.Center = glm::vec2(world[3]);
            glm::vec2 up = glm::vec2(world * glm::vec4(0, 1, 0, 0));
            d.Axis = glm::length(up) > 1e-6f ? glm::normalize(up) : glm::vec2(0, 1);
            if (l.Type == Light2DType::Point)
            {
                const float r = std::max(l.OuterRadius, 0.001f);
                d.Min = d.Center - glm::vec2(r);
                d.Max = d.Center + glm::vec2(r);
                PushRect(d.Geometry, d.Min, d.Max);
                return true;
            }
            if (l.Type == Light2DType::Sprite)
            {
                d.Cookie = LoadSprite(l.Sprite, l.LoadedSprite, l.SpriteTexture);
                if (!d.Cookie)
                    return false;
                const glm::vec2 h = glm::vec2((float)d.Cookie->GetWidth(), (float)d.Cookie->GetHeight()) / 200.0f;
                const glm::vec2 p[4] = { ToWorld(world, { -h.x, -h.y }), ToWorld(world, { h.x, -h.y }), ToWorld(world, { h.x, h.y }), ToWorld(world, { -h.x, h.y }) };
                const glm::vec2 uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                PushQuad(d.Geometry, p, uv);
                d.Min = glm::min(glm::min(p[0], p[1]), glm::min(p[2], p[3]));
                d.Max = glm::max(glm::max(p[0], p[1]), glm::max(p[2], p[3]));
                return true;
            }
            if (l.Type == Light2DType::Freeform)
            {
                if (l.Points.size() < 3)
                    return false;
                for (size_t i = 0; i < l.Points.size() && i < 32; ++i)
                    d.Poly.push_back(ToWorld(world, l.Points[i]));
                d.Min = d.Max = d.Poly[0];
                for (auto& p : d.Poly)
                {
                    d.Min = glm::min(d.Min, p);
                    d.Max = glm::max(d.Max, p);
                }
                d.Min -= glm::vec2(std::max(l.FalloffDistance, 0.0f));
                d.Max += glm::vec2(std::max(l.FalloffDistance, 0.0f));
                PushRect(d.Geometry, d.Min, d.Max);
                return true;
            }
            return false;
        }

        void SetLightUniforms(Shader& sh, const LightDraw& d, float factor)
        {
            const Light2DComponent& l = *d.L;
            sh.SetInt("u_Type", l.Type == Light2DType::Sprite ? 2 : l.Type == Light2DType::Freeform ? 3 : 1);
            sh.SetFloat3("u_Color", l.Color * std::max(l.Intensity, 0.0f) * factor);
            sh.SetFloat2("u_Center", d.Center);
            sh.SetFloat2("u_Axis", d.Axis);
            sh.SetFloat2("u_Radii", glm::vec2(std::max(0.0f, std::min(l.InnerRadius, l.OuterRadius)), std::max(l.OuterRadius, 0.001f)));
            const float outer = glm::radians(std::clamp(l.OuterAngle, 0.0f, 360.0f)) * 0.5f;
            const float inner = std::min(glm::radians(std::clamp(l.InnerAngle, 0.0f, 360.0f)) * 0.5f, outer);
            sh.SetFloat2("u_HalfAngles", glm::vec2(inner, l.OuterAngle >= 359.9f ? 4.0f : outer));
            sh.SetFloat("u_FalloffExp", 1.0f + std::clamp(l.FalloffStrength, 0.0f, 1.0f) * 3.0f);
            if (l.Type == Light2DType::Freeform)
            {
                sh.SetInt("u_PointCount", (int)d.Poly.size());
                for (size_t i = 0; i < d.Poly.size(); ++i)
                    sh.SetFloat2("u_Points[" + std::to_string(i) + "]", d.Poly[i]);
                sh.SetFloat("u_FalloffDistance", std::max(l.FalloffDistance, 0.0f));
            }
            if (d.Cookie)
            {
                d.Cookie->Bind(0);
                sh.SetInt("u_Cookie", 0);
            }
        }

        bool Overlaps(const glm::vec2& aMin, const glm::vec2& aMax, const glm::vec2& bMin, const glm::vec2& bMax)
        {
            return aMin.x <= bMax.x && aMax.x >= bMin.x && aMin.y <= bMax.y && aMax.y >= bMin.y;
        }

        // Kamera görünür dikdörtgeni (z = 0 düzlemi; ortografik/perspektif)
        void ViewBounds(const glm::mat4& viewProjection, glm::vec2& mn, glm::vec2& mx)
        {
            glm::mat4 inv = glm::inverse(viewProjection);
            mn = glm::vec2(1e30f);
            mx = glm::vec2(-1e30f);
            for (int i = 0; i < 4; ++i)
            {
                glm::vec2 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f);
                glm::vec4 a = inv * glm::vec4(ndc, -1.0f, 1.0f), b = inv * glm::vec4(ndc, 1.0f, 1.0f);
                glm::vec3 p0 = glm::vec3(a) / a.w, p1 = glm::vec3(b) / b.w;
                glm::vec3 dir = p1 - p0;
                glm::vec2 hit = glm::vec2(p0);
                if (std::fabs(dir.z) > 1e-6f)
                {
                    float t = -p0.z / dir.z;
                    if (t > 0.0f)
                        hit = glm::vec2(p0 + dir * std::min(t, 1.0f));
                    else
                        hit = glm::vec2(p1);
                }
                mn = glm::min(mn, hit);
                mx = glm::max(mx, hit);
            }
            // Perspektif + eğik görünümde güvenli pay
            glm::vec2 pad = (mx - mn) * 0.1f;
            mn -= pad;
            mx += pad;
        }

        bool s_PassLit = false;
    }

    namespace Lighting2D
    {
        bool BeginPass(Scene& scene, const glm::mat4& viewProjection)
        {
            s_PassLit = false;
            auto& reg = scene.m_Registry;
            auto lights = reg.view<Light2DComponent, TransformComponent>();
            auto masks = reg.view<SpriteMaskComponent, TransformComponent>();
            bool anyLight = false, anyMask = false;
            for (auto e : lights) if (scene.IsEntityActive(e)) { anyLight = true; break; }
            for (auto e : masks) if (scene.IsEntityActive(e)) { anyMask = true; break; }
            if ((!anyLight && !anyMask) || !EnsureResources())
                return false;

            GLint prevFbo = 0, vp[4];
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
            glGetIntegerv(GL_VIEWPORT, vp);
            if (vp[2] <= 0 || vp[3] <= 0)
                return false;
            GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST), prevBlend = glIsEnabled(GL_BLEND), prevCull = glIsEnabled(GL_CULL_FACE);
            GLboolean prevDepthMask;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
            glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            State& st = S();

            // ---- Işık dokusu
            if (anyLight)
            {
                glm::vec3 ambient(0.0f);
                std::vector<LightDraw> draws;
                glm::vec2 viewMin, viewMax;
                ViewBounds(viewProjection, viewMin, viewMax);
                for (auto e : lights)
                {
                    if (!scene.IsEntityActive(e))
                        continue;
                    auto& l = lights.get<Light2DComponent>(e);
                    if (l.Type == Light2DType::Global)
                    {
                        ambient += l.Color * std::max(l.Intensity, 0.0f);
                        continue;
                    }
                    LightDraw d;
                    if (BuildLight(l, lights.get<TransformComponent>(e).WorldTransform, d) && Overlaps(d.Min, d.Max, viewMin, viewMax))
                        draws.push_back(std::move(d));
                }
                std::vector<Caster> casters;
                for (auto& d : draws)
                    if (d.L->Shadows)
                    {
                        casters = GatherCasters(scene);
                        break;
                    }

                LightTarget* t = AcquireTarget(vp[2], vp[3]);
                glBindFramebuffer(GL_FRAMEBUFFER, t->Fbo);
                glViewport(0, 0, vp[2], vp[3]);
                glClearColor(ambient.r, ambient.g, ambient.b, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);

                Shader& light = *st.LightShader;
                Shader& stencil = *st.StencilShader;
                for (auto& d : draws)
                {
                    const Light2DComponent& l = *d.L;
                    const float strength = std::clamp(l.ShadowStrength, 0.0f, 1.0f);
                    bool shadowed = false;
                    if (l.Shadows && strength > 0.0f && !casters.empty())
                    {
                        // Gölge hacimleri: her kenar ışıktan uzağa uzatılır (stencil = 1)
                        const float far = glm::length(d.Max - d.Min) * 2.0f + 10.0f;
                        std::vector<glm::vec4> volumes, clears;
                        for (auto& c : casters)
                        {
                            if (!Overlaps(c.Min, c.Max, d.Min, d.Max))
                                continue;
                            const size_t n = c.Poly.size();
                            for (size_t i = 0; i < n; ++i)
                            {
                                glm::vec2 a = c.Poly[i], b = c.Poly[(i + 1) % n];
                                glm::vec2 da = a - d.Center, db = b - d.Center;
                                if (glm::length(da) < 1e-5f || glm::length(db) < 1e-5f)
                                    continue;
                                glm::vec2 a2 = a + glm::normalize(da) * far, b2 = b + glm::normalize(db) * far;
                                volumes.emplace_back(a, 0, 0); volumes.emplace_back(b, 0, 0); volumes.emplace_back(b2, 0, 0);
                                volumes.emplace_back(a, 0, 0); volumes.emplace_back(b2, 0, 0); volumes.emplace_back(a2, 0, 0);
                            }
                            if (!c.SelfShadows)
                                for (size_t i = 1; i + 1 < n; ++i)
                                {
                                    clears.emplace_back(c.Poly[0], 0, 0);
                                    clears.emplace_back(c.Poly[i], 0, 0);
                                    clears.emplace_back(c.Poly[i + 1], 0, 0);
                                }
                        }
                        if (!volumes.empty())
                        {
                            shadowed = true;
                            glClear(GL_STENCIL_BUFFER_BIT);
                            glEnable(GL_STENCIL_TEST);
                            glStencilMask(0xFF);
                            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                            stencil.Bind();
                            stencil.SetMat4("u_ViewProjection", viewProjection);
                            stencil.SetInt("u_UseTexture", 0);
                            glStencilFunc(GL_ALWAYS, 1, 0xFF);
                            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                            DrawTriangles(volumes);
                            if (!clears.empty())
                            {
                                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                                DrawTriangles(clears);
                            }
                            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                            glStencilMask(0x00);
                        }
                    }
                    light.Bind();
                    light.SetMat4("u_ViewProjection", viewProjection);
                    if (shadowed)
                    {
                        glStencilFunc(GL_EQUAL, 0, 0xFF);
                        SetLightUniforms(light, d, 1.0f);
                        DrawTriangles(d.Geometry);
                        if (strength < 1.0f)
                        {
                            glStencilFunc(GL_EQUAL, 1, 0xFF);
                            SetLightUniforms(light, d, 1.0f - strength);
                            DrawTriangles(d.Geometry);
                        }
                        glStencilMask(0xFF);
                        glDisable(GL_STENCIL_TEST);
                    }
                    else
                    {
                        SetLightUniforms(light, d, 1.0f);
                        DrawTriangles(d.Geometry);
                    }
                }
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
                glViewport(vp[0], vp[1], vp[2], vp[3]);
                Renderer2D::SetLighting(t->Tex, glm::vec2((float)vp[0], (float)vp[1]));
                s_PassLit = true;
            }

            // ---- Sprite maskeleri: sahne tamponunun stencil'ine (bit 0)
            if (anyMask)
            {
                struct MaskDraw { std::shared_ptr<Texture2D> Tex; float Cutoff; std::vector<glm::vec4> Quad; };
                std::vector<MaskDraw> batches;
                for (auto e : masks)
                {
                    if (!scene.IsEntityActive(e))
                        continue;
                    auto& m = masks.get<SpriteMaskComponent>(e);
                    const glm::mat4& world = masks.get<TransformComponent>(e).WorldTransform;
                    auto tex = LoadSprite(m.Sprite, m.LoadedSprite, m.SpriteTexture);
                    glm::vec2 h(0.5f);
                    if (tex)
                        h = glm::vec2((float)tex->GetWidth(), (float)tex->GetHeight()) / (2.0f * std::max(m.PixelsPerUnit, 0.001f));
                    const glm::vec2 p[4] = { ToWorld(world, { -h.x, -h.y }), ToWorld(world, { h.x, -h.y }), ToWorld(world, { h.x, h.y }), ToWorld(world, { -h.x, h.y }) };
                    const glm::vec2 uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                    std::vector<glm::vec4> quad;
                    PushQuad(quad, p, uv);
                    batches.push_back({ tex, std::clamp(m.AlphaCutoff, 0.0f, 1.0f), std::move(quad) });
                }
                glStencilMask(0xFF);
                glClear(GL_STENCIL_BUFFER_BIT);
                glEnable(GL_STENCIL_TEST);
                glStencilFunc(GL_ALWAYS, 1, 0x01);
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                Shader& stencil = *st.StencilShader;
                stencil.Bind();
                stencil.SetMat4("u_ViewProjection", viewProjection);
                for (auto& b : batches)
                {
                    stencil.SetInt("u_UseTexture", b.Tex ? 1 : 0);
                    stencil.SetFloat("u_AlphaCutoff", b.Cutoff);
                    if (b.Tex)
                    {
                        b.Tex->Bind(0);
                        stencil.SetInt("u_Texture", 0);
                    }
                    DrawTriangles(b.Quad);
                }
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                glDisable(GL_STENCIL_TEST);
            }

            if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
            glDepthMask(prevDepthMask);
            return s_PassLit;
        }

        void EndPass(Scene& scene, const glm::mat4& viewProjection)
        {
            Renderer2D::SetLighting(0, glm::vec2(0.0f));
            Renderer2D::SetLit(false);
            Renderer2D::SetMaskInteraction(0);
            if (!s_PassLit)
                return;
            s_PassLit = false;
            // Hacim ışıkları: sahnenin üstüne eklenir (sis / toz içindeki hüzme)
            auto lights = scene.m_Registry.view<Light2DComponent, TransformComponent>();
            std::vector<LightDraw> draws;
            for (auto e : lights)
            {
                auto& l = lights.get<Light2DComponent>(e);
                if (l.VolumeIntensity <= 0.0f || l.Type == Light2DType::Global || !scene.IsEntityActive(e))
                    continue;
                LightDraw d;
                if (BuildLight(l, lights.get<TransformComponent>(e).WorldTransform, d))
                    draws.push_back(std::move(d));
            }
            if (draws.empty() || !EnsureResources())
                return;
            GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST), prevBlend = glIsEnabled(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            for (GLuint i = 1; i < 4; ++i)
                glColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            Shader& light = *S().LightShader;
            light.Bind();
            light.SetMat4("u_ViewProjection", viewProjection);
            for (auto& d : draws)
            {
                SetLightUniforms(light, d, std::clamp(d.L->VolumeIntensity, 0.0f, 4.0f));
                DrawTriangles(d.Geometry);
            }
            for (GLuint i = 1; i < 4; ++i)
                glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_TRUE);
            if (prevDepthTest) glEnable(GL_DEPTH_TEST);
            if (!prevBlend) glDisable(GL_BLEND);
        }

        glm::vec3 SampleLight(Scene& scene, const glm::vec2& p)
        {
            glm::vec3 sum(0.0f);
            auto lights = scene.m_Registry.view<Light2DComponent, TransformComponent>();
            for (auto e : lights)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                auto& l = lights.get<Light2DComponent>(e);
                const glm::vec3 c = l.Color * std::max(l.Intensity, 0.0f);
                if (l.Type == Light2DType::Global)
                {
                    sum += c;
                    continue;
                }
                if (l.Type != Light2DType::Point)
                    continue;
                const glm::mat4& w = lights.get<TransformComponent>(e).WorldTransform;
                glm::vec2 v = p - glm::vec2(w[3]);
                float d = glm::length(v);
                float inner = std::max(0.0f, std::min(l.InnerRadius, l.OuterRadius)), outer = std::max(l.OuterRadius, 0.001f);
                float t = std::clamp((d - inner) / std::max(outer - inner, 1e-4f), 0.0f, 1.0f);
                float atten = std::pow(1.0f - t, 1.0f + std::clamp(l.FalloffStrength, 0.0f, 1.0f) * 3.0f);
                if (l.OuterAngle < 359.9f && d > 1e-5f)
                {
                    glm::vec2 up = glm::vec2(w * glm::vec4(0, 1, 0, 0));
                    glm::vec2 axis = glm::length(up) > 1e-6f ? glm::normalize(up) : glm::vec2(0, 1);
                    float ang = std::acos(std::clamp(glm::dot(v / d, axis), -1.0f, 1.0f));
                    float outerHalf = glm::radians(l.OuterAngle) * 0.5f, innerHalf = std::min(glm::radians(l.InnerAngle) * 0.5f, outerHalf);
                    float k = std::clamp((ang - innerHalf) / std::max(outerHalf - innerHalf, 1e-3f), 0.0f, 1.0f);
                    atten *= 1.0f - k * k * (3.0f - 2.0f * k);
                }
                sum += c * atten;
            }
            return sum;
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            entt::entity e = (entt::entity)entity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e) || !reg.all_of<TransformComponent>(e))
                return;
            const glm::mat4& world = reg.get<TransformComponent>(e).WorldTransform;
            const glm::vec3 c = glm::vec3(world[3]);
            if (auto* l = reg.try_get<Light2DComponent>(e))
            {
                const glm::vec4 col(1.0f, 0.9f, 0.4f, 1.0f);
                if (l->Type == Light2DType::Point)
                {
                    glm::vec2 up = glm::vec2(world * glm::vec4(0, 1, 0, 0));
                    float base = std::atan2(up.y, up.x);
                    if (l->OuterAngle >= 359.9f)
                    {
                        DebugDraw::Circle(c, glm::vec3(0, 0, 1), l->OuterRadius, col, 0.0f, false);
                        if (l->InnerRadius > 0.0f)
                            DebugDraw::Circle(c, glm::vec3(0, 0, 1), l->InnerRadius, col * glm::vec4(1, 1, 1, 0.5f), 0.0f, false);
                    }
                    else
                    {
                        float half = glm::radians(l->OuterAngle) * 0.5f;
                        DebugDraw::Arc(c, glm::vec3(0, 0, 1), l->OuterRadius, glm::degrees(base - half), glm::degrees(base + half), col, 0.0f, false);
                        for (float a : { base - half, base + half })
                            DebugDraw::Line(c, c + glm::vec3(std::cos(a), std::sin(a), 0.0f) * l->OuterRadius, col, 0.0f, false);
                    }
                }
                else if (l->Type == Light2DType::Freeform && l->Points.size() >= 2)
                {
                    for (size_t i = 0; i < l->Points.size(); ++i)
                    {
                        glm::vec2 a = ToWorld(world, l->Points[i]), b = ToWorld(world, l->Points[(i + 1) % l->Points.size()]);
                        DebugDraw::Line(glm::vec3(a, c.z), glm::vec3(b, c.z), col, 0.0f, false);
                    }
                }
            }
            if (auto* sc = reg.try_get<ShadowCaster2DComponent>(e))
            {
                auto shape = CasterShape(scene, e, *sc);
                for (size_t i = 0; i < shape.size(); ++i)
                {
                    glm::vec2 a = ToWorld(world, shape[i]), b = ToWorld(world, shape[(i + 1) % shape.size()]);
                    DebugDraw::Line(glm::vec3(a, c.z), glm::vec3(b, c.z), glm::vec4(0.6f, 0.6f, 0.65f, 1.0f), 0.0f, false);
                }
            }
            if (auto* m = reg.try_get<SpriteMaskComponent>(e))
            {
                glm::vec2 h(0.5f);
                if (m->SpriteTexture)
                    h = glm::vec2((float)m->SpriteTexture->GetWidth(), (float)m->SpriteTexture->GetHeight()) / (2.0f * std::max(m->PixelsPerUnit, 0.001f));
                const glm::vec2 p[4] = { ToWorld(world, { -h.x, -h.y }), ToWorld(world, { h.x, -h.y }), ToWorld(world, { h.x, h.y }), ToWorld(world, { -h.x, h.y }) };
                for (int i = 0; i < 4; ++i)
                    DebugDraw::Line(glm::vec3(p[i], c.z), glm::vec3(p[(i + 1) % 4], c.z), glm::vec4(1.0f, 0.5f, 0.2f, 1.0f), 0.0f, false);
            }
        }

        void Shutdown()
        {
            if (!s_State)
                return;
            for (auto& t : s_State->Targets)
            {
                glDeleteFramebuffers(1, &t.Fbo);
                glDeleteTextures(1, &t.Tex);
                glDeleteRenderbuffers(1, &t.Rbo);
            }
            if (s_State->Vao) glDeleteVertexArrays(1, &s_State->Vao);
            if (s_State->Vbo) glDeleteBuffers(1, &s_State->Vbo);
            delete s_State;
            s_State = nullptr;
        }
    }

    void RegisterLighting2DComponents()
    {
        using L = Light2DComponent;
        ComponentDef<L>("Light 2D", "Light2DComponent", "Rendering", ICON_FA_LIGHTBULB).Color({ 1.0f, 0.9f, 0.45f, 1.0f })
            .Describe("2D light (URP 2D): sprites and tilemaps are multiplied by the light; with no Light 2D in the scene everything stays unlit.")
            .Field("Type", "Light Type", &L::Type).Options({ "Global", "Point", "Sprite", "Freeform" })
            .Field("Color", "Color", &L::Color, FieldKind::Color3)
            .Field("Intensity", "Intensity", &L::Intensity).Range(0.0f, 20.0f, 0.01f)
            .Field("InnerRadius", "Inner Radius", &L::InnerRadius).Range(0.0f, 1000.0f, 0.05f).VisibleIf([](const L& l) { return l.Type == Light2DType::Point; })
            .Field("OuterRadius", "Outer Radius", &L::OuterRadius).Range(0.0f, 1000.0f, 0.05f).VisibleIf([](const L& l) { return l.Type == Light2DType::Point; })
            .Field("InnerAngle", "Inner Spot Angle", &L::InnerAngle).Range(0.0f, 360.0f, 0.5f).Format("%.1f°").VisibleIf([](const L& l) { return l.Type == Light2DType::Point; })
            .Field("OuterAngle", "Outer Spot Angle", &L::OuterAngle).Range(0.0f, 360.0f, 0.5f).Format("%.1f°").VisibleIf([](const L& l) { return l.Type == Light2DType::Point; })
            .Field("FalloffStrength", "Falloff Strength", &L::FalloffStrength).Range(0.0f, 1.0f, 0.01f).VisibleIf([](const L& l) { return l.Type != Light2DType::Global && l.Type != Light2DType::Sprite; })
            .Field("Sprite", "Sprite", &L::Sprite, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga").VisibleIf([](const L& l) { return l.Type == Light2DType::Sprite; })
            .Field("Points", "Shape", &L::Points).VisibleIf([](const L& l) { return l.Type == Light2DType::Freeform; })
            .Field("FalloffDistance", "Falloff", &L::FalloffDistance).Range(0.0f, 100.0f, 0.01f).VisibleIf([](const L& l) { return l.Type == Light2DType::Freeform; })
            .Field("Shadows", "Shadows", &L::Shadows).Header("Shadows").VisibleIf([](const L& l) { return l.Type != Light2DType::Global; })
            .Field("ShadowStrength", "Shadow Strength", &L::ShadowStrength).Range(0.0f, 1.0f, 0.01f).VisibleIf([](const L& l) { return l.Type != Light2DType::Global && l.Shadows; })
            .Field("VolumeIntensity", "Volumetric Intensity", &L::VolumeIntensity).Range(0.0f, 4.0f, 0.01f).VisibleIf([](const L& l) { return l.Type != Light2DType::Global; })
                .Tooltip("Adds the light on top of everything, like a beam through dust or fog.");

        using C = ShadowCaster2DComponent;
        ComponentDef<C>("Shadow Caster 2D", "ShadowCaster2DComponent", "Rendering", ICON_FA_CLOUD)
            .Describe("Casts hard 2D shadows from Light 2D lights with Shadows on. Shape: the 2D collider, else the sprite bounds, or a custom polygon.")
            .Field("CastsShadows", "Casts Shadows", &C::CastsShadows)
            .Field("SelfShadows", "Self Shadows", &C::SelfShadows)
            .Field("Shape", "Shape", &C::Shape).Options({ "Auto (Collider / Sprite)", "Custom" })
            .Field("Points", "Points", &C::Points).VisibleIf([](const C& c) { return c.Shape == ShadowCasterShape::Custom; });

        using M = SpriteMaskComponent;
        ComponentDef<M>("Sprite Mask", "SpriteMaskComponent", "Rendering", ICON_FA_MASKS_THEATER)
            .Describe("Hides or reveals Sprite Renderers whose Mask Interaction is Visible Inside / Outside Mask.")
            .Field("Sprite", "Sprite", &M::Sprite, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga").Tooltip("Empty = a 1 x 1 rectangle (scaled by the transform).")
            .Field("AlphaCutoff", "Alpha Cutoff", &M::AlphaCutoff).Range(0.0f, 1.0f, 0.01f)
            .Field("PixelsPerUnit", "Pixels Per Unit", &M::PixelsPerUnit).Range(1.0f, 10000.0f, 1.0f);
    }
}
