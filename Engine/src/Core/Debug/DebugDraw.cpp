#include "DebugDraw.h"
#include "DebugDrawInternal.h"
#include "DebugPalette.h"
#include "DebugSettings.h"
#include "DebugTimer.h"
#include "DebugSolidDraw.h"
#include "DebugTextDraw.h"
#include "DebugGizmoDraw.h"
#include "DebugCameraDraw.h"
#include "DebugShapeDraw.h"
#include "DebugPhysicsDraw.h"
#include "DebugGridDraw.h"
#include "DebugMetrics.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/Buffer.h"
#include "Renderer/RHI/VertexArray.h"
#include "Renderer/RHI/RenderCommand.h"

#include <glad/glad.h>
#include <vector>

namespace Conqueror
{
    struct DebugVertex
    {
        glm::vec3 Position;
        glm::vec4 Color;
    };

    struct DebugDrawData
    {
        std::vector<DebugVertex> LineVertices;
        
        std::shared_ptr<VertexArray> LineVA;
        std::shared_ptr<VertexBuffer> LineVB;
        std::shared_ptr<Shader> LineShader;
        
        uint32_t MaxVertices = 100000; // Increased max vertices
        
        std::vector<glm::vec3> UnitSphereLines;

        std::vector<DebugVertex> LineVerticesNoDepth;
        glm::vec3 CameraPosition = glm::vec3(0.0f);
        float GridFadeRadius = 500.0f;
        float GridStep = 1.0f;
    };

    static DebugDrawData* s_Data = nullptr;

    void DebugDraw::Init()
    {
        s_Data = new DebugDrawData();
        s_Data->MaxVertices = DebugSettings::Get().MaxLineVertices;
        
        // Vertex Array
        s_Data->LineVA = VertexArray::Create();
        
        // Vertex Buffer
        s_Data->LineVB = VertexBuffer::Create(s_Data->MaxVertices * sizeof(DebugVertex));
        s_Data->LineVB->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float4, "a_Color" }
        });
        s_Data->LineVA->AddVertexBuffer(s_Data->LineVB);
        
        // Shader
        std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec4 a_Color;
            
            uniform mat4 u_ViewProjection;
            
            out vec4 v_Color;
            out vec3 v_Position;
            
            void main()
            {
                v_Color = a_Color;
                v_Position = a_Position;
                gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
            }
        )";
        
        std::string fragmentSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 color;
            
            in vec4 v_Color;
            in vec3 v_Position;
            
            uniform vec3 u_CameraPosition;
            uniform float u_FadeRadius;
            uniform float u_GridStep;
            
            void main()
            {
                float alpha = v_Color.a;
                bool isGridFade = false;
                
                if (alpha < 0.0)
                {
                    isGridFade = true;
                    alpha = -alpha;
                }
                
                if (isGridFade && u_FadeRadius > 0.0)
                {
                    vec3 evalPos = v_Position;
                    if (u_GridStep > 0.0)
                    {
                        evalPos = floor(v_Position / u_GridStep) * u_GridStep;
                    }
                    
                    float dist = distance(evalPos, u_CameraPosition);
                    float fadeStart = u_FadeRadius * 0.1;
                    float fade = 1.0 - smoothstep(fadeStart, u_FadeRadius, dist);
                    alpha *= fade;
                }
                
                if (alpha < 0.01)
                    discard;
                
                color = vec4(v_Color.rgb, alpha);
            }
        )";
        
        s_Data->LineShader = Shader::Create("DebugLine", vertexSrc, fragmentSrc);

        // Precompute Unit Sphere Lines (16 segments)
        int segments = 16;
        float angleStep = 2.0f * 3.14159265359f / segments;
        for (int i = 0; i < segments; i++)
        {
            float a1 = i * angleStep, a2 = (i + 1) * angleStep;
            // XY
            s_Data->UnitSphereLines.push_back({cos(a1), sin(a1), 0.0f});
            s_Data->UnitSphereLines.push_back({cos(a2), sin(a2), 0.0f});
            // XZ
            s_Data->UnitSphereLines.push_back({cos(a1), 0.0f, sin(a1)});
            s_Data->UnitSphereLines.push_back({cos(a2), 0.0f, sin(a2)});
            // YZ
            s_Data->UnitSphereLines.push_back({0.0f, cos(a1), sin(a1)});
            s_Data->UnitSphereLines.push_back({0.0f, cos(a2), sin(a2)});
        }

        DebugTimer::Init();
        DebugSolidDraw::Init();
        DebugTextDraw::Init();
    }

    void DebugDraw::Shutdown()
    {
        DebugTextDraw::Shutdown();
        DebugSolidDraw::Shutdown();
        DebugTimer::Shutdown();

        delete s_Data;
        s_Data = nullptr;
    }

    namespace DebugDrawInternal
    {
        void SubmitLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, bool depthTested)
        {
            DebugDraw::Line(start, end, color, 0.0f, depthTested);
        }

        void SubmitLineImmediate(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, bool depthTested)
        {
            if (!s_Data)
                return;

            auto& buffer = depthTested ? s_Data->LineVertices : s_Data->LineVerticesNoDepth;
            if (buffer.size() + 2 > s_Data->MaxVertices)
                return;

            buffer.push_back({ start, color });
            buffer.push_back({ end, color });
        }

        uint32_t GetLineVertexCount()
        {
            if (!s_Data)
                return 0;
            return static_cast<uint32_t>(s_Data->LineVertices.size() + s_Data->LineVerticesNoDepth.size());
        }

        uint32_t GetLineCount()
        {
            return GetLineVertexCount() / 2;
        }
    }

    static void PushLineImmediate(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, bool depthTested)
    {
        DebugDrawInternal::SubmitLineImmediate(start, end, color, depthTested);
    }

    void DebugDraw::Update(float deltaTime)
    {
        DebugMetrics::ResetFrame();
        DebugTimer::Update(deltaTime);
    }

    void DebugDraw::Line(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!s_Data || !DebugSettings::Get().Enabled || !DebugSettings::Get().IsCategoryActive(DebugCategory::Wireframe))
            return;

        auto drawImmediate = [=]()
        {
            PushLineImmediate(start, end, color, depthTested);
        };

        if (duration > 0.0f)
            DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, drawImmediate);
        else
            drawImmediate();
    }

    void DebugDraw::Box(const glm::vec3& center, const glm::vec3& size, const glm::vec4& color, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
            {
                Box(center, size, color, 0.0f, depthTested);
            });
            return;
        }

        glm::vec3 min = center - size * 0.5f;
        glm::vec3 max = center + size * 0.5f;
        
        // Alt yüz
        Line(glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z), color);
        Line(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, min.y, max.z), color);
        Line(glm::vec3(max.x, min.y, max.z), glm::vec3(min.x, min.y, max.z), color);
        Line(glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, min.y, min.z), color);
        
        // Üst yüz
        Line(glm::vec3(min.x, max.y, min.z), glm::vec3(max.x, max.y, min.z), color);
        Line(glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z), color);
        Line(glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z), color);
        Line(glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z), color);
        
        // Dikey kenarlar
        Line(glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, max.y, min.z), color);
        Line(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, max.y, min.z), color);
        Line(glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, max.y, max.z), color);
        Line(glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, max.y, max.z), color, duration, depthTested);
    }

    void DebugDraw::Box(const glm::mat4& transform, const glm::vec4& color, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
            {
                Box(transform, color, 0.0f, depthTested);
            });
            return;
        }

        glm::vec3 corners[8] = {
            glm::vec3(-0.5f, -0.5f, -0.5f),
            glm::vec3( 0.5f, -0.5f, -0.5f),
            glm::vec3( 0.5f, -0.5f,  0.5f),
            glm::vec3(-0.5f, -0.5f,  0.5f),
            glm::vec3(-0.5f,  0.5f, -0.5f),
            glm::vec3( 0.5f,  0.5f, -0.5f),
            glm::vec3( 0.5f,  0.5f,  0.5f),
            glm::vec3(-0.5f,  0.5f,  0.5f)
        };
        
        for (int i = 0; i < 8; i++)
        {
            glm::vec4 transformed = transform * glm::vec4(corners[i], 1.0f);
            corners[i] = glm::vec3(transformed);
        }
        
        // Alt yüz
        Line(corners[0], corners[1], color);
        Line(corners[1], corners[2], color);
        Line(corners[2], corners[3], color);
        Line(corners[3], corners[0], color);
        
        // Üst yüz
        Line(corners[4], corners[5], color);
        Line(corners[5], corners[6], color);
        Line(corners[6], corners[7], color);
        Line(corners[7], corners[4], color);
        
        // Dikey kenarlar
        Line(corners[0], corners[4], color);
        Line(corners[1], corners[5], color);
        Line(corners[2], corners[6], color);
        Line(corners[3], corners[7], color, duration, depthTested);
    }

    void DebugDraw::Sphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
            {
                Sphere(center, radius, color, 0.0f, depthTested);
            });
            return;
        }

        // Optimizasyon: Önceden hesaplanmış noktaları kullan, per-frame sin/cos hesaplama!
        for (size_t i = 0; i < s_Data->UnitSphereLines.size(); i += 2)
        {
            glm::vec3 p1 = center + s_Data->UnitSphereLines[i] * radius;
            glm::vec3 p2 = center + s_Data->UnitSphereLines[i+1] * radius;
            Line(p1, p2, color, duration, depthTested);
        }
    }

    void DebugDraw::Capsule(const glm::vec3& center, float radius, float height, const glm::vec4& color, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
            {
                Capsule(center, radius, height, color, 0.0f, depthTested);
            });
            return;
        }

        float halfHeight = height * 0.5f - radius;
        
        // Üst sphere
        Sphere(center + glm::vec3(0, halfHeight, 0), radius, color, duration, depthTested);
        
        // Alt sphere
        Sphere(center - glm::vec3(0, halfHeight, 0), radius, color, duration, depthTested);
        
        // Dikey çizgiler
        Line(center + glm::vec3(radius, -halfHeight, 0), center + glm::vec3(radius, halfHeight, 0), color, duration, depthTested);
        Line(center + glm::vec3(-radius, -halfHeight, 0), center + glm::vec3(-radius, halfHeight, 0), color, duration, depthTested);
        Line(center + glm::vec3(0, -halfHeight, radius), center + glm::vec3(0, halfHeight, radius), color, duration, depthTested);
        Line(center + glm::vec3(0, -halfHeight, -radius), center + glm::vec3(0, halfHeight, -radius), color, duration, depthTested);
    }

    void DebugDraw::Ray(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
            {
                Ray(origin, direction, length, color, 0.0f, depthTested);
            });
            return;
        }

        glm::vec3 end = origin + glm::normalize(direction) * length;
        Line(origin, end, color, duration, depthTested);
        
        // Ok başı
        glm::vec3 right = glm::cross(direction, glm::vec3(0, 1, 0));
        if (glm::length(right) < 0.001f)
            right = glm::cross(direction, glm::vec3(1, 0, 0));
        right = glm::normalize(right) * 0.2f;
        
        glm::vec3 arrowEnd1 = end - glm::normalize(direction) * 0.5f + right;
        glm::vec3 arrowEnd2 = end - glm::normalize(direction) * 0.5f - right;
        
        Line(end, arrowEnd1, color, duration, depthTested);
        Line(end, arrowEnd2, color, duration, depthTested);
    }

    void DebugDraw::Grid(float size, float step, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Grid))
            return;

        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Grid, [=]()
            {
                Grid(size, step, color, 0.0f, depthTested);
            });
            return;
        }

        for (float i = -size; i <= size; i += step)
        {
            Line(glm::vec3(-size, 0.0f, i), glm::vec3(size, 0.0f, i), color, duration, depthTested);
            Line(glm::vec3(i, 0.0f, -size), glm::vec3(i, 0.0f, size), color, duration, depthTested);
        }
    }

    void DebugDraw::SolidBox(const glm::mat4& transform, const glm::vec4& color, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Solid, [=]()
            {
                SolidBox(transform, color, 0.0f, depthTested);
            });
            return;
        }

        DebugSolidDraw::Box(transform, color, depthTested);
    }

    void DebugDraw::SolidSphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Solid, [=]()
            {
                SolidSphere(center, radius, color, 0.0f, depthTested);
            });
            return;
        }

        DebugSolidDraw::Sphere(center, radius, color, depthTested);
    }

    void DebugDraw::Text3D(const glm::vec3& position, const std::string& text, const glm::vec4& color, float scale, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Text, [=]()
            {
                Text3D(position, text, color, scale, 0.0f, depthTested);
            });
            return;
        }

        DebugTextDraw::Draw(position, text, color, scale, depthTested);
    }

    static void RenderLineBatch(std::vector<DebugVertex>& vertices, bool depthTested, float lineWidth)
    {
        if (vertices.empty())
            return;

        uint32_t vertexCount = static_cast<uint32_t>(vertices.size());
        if (vertexCount > s_Data->MaxVertices)
            vertexCount = s_Data->MaxVertices - (s_Data->MaxVertices % 2);

        if (depthTested)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);

        s_Data->LineVB->SetData(vertices.data(), vertexCount * sizeof(DebugVertex));
        s_Data->LineShader->Bind();
        RenderCommand::SetLineWidth(lineWidth);
        RenderCommand::DrawLines(s_Data->LineVA, vertexCount);
    }

    void DebugDraw::Render(const glm::mat4& viewProjection)
    {
        if (!s_Data || !DebugSettings::Get().Enabled)
            return;

        DebugTimer::FlushActive();
        DebugTextDraw::Render(viewProjection, s_Data->CameraPosition);

        if (s_Data->LineVertices.empty() && s_Data->LineVerticesNoDepth.empty())
        {
            DebugSolidDraw::Render(viewProjection);
            DebugMetrics::RecordLineDraw(0, 0);
            DebugMetrics::RecordSolidDraw(DebugSolidDraw::GetTriangleCount(), DebugSolidDraw::GetVertexCount());
            DebugMetrics::RecordTextDraw(DebugTextDraw::GetEntryCount());
            DebugMetrics::RecordTimedEntry(DebugTimer::GetActiveCount());
            return;
        }

        s_Data->LineShader->Bind();
        s_Data->LineShader->SetMat4("u_ViewProjection", viewProjection);
        s_Data->LineShader->SetFloat3("u_CameraPosition", s_Data->CameraPosition);
        s_Data->LineShader->SetFloat("u_FadeRadius", s_Data->GridFadeRadius);
        s_Data->LineShader->SetFloat("u_GridStep", s_Data->GridStep);

        const float lineWidth = DebugSettings::Get().LineWidth;
        RenderLineBatch(s_Data->LineVertices, true, lineWidth);
        RenderLineBatch(s_Data->LineVerticesNoDepth, false, lineWidth);

        if (DebugSettings::Get().DepthTestLines)
            glEnable(GL_DEPTH_TEST);

        DebugSolidDraw::Render(viewProjection);

        DebugMetrics::RecordLineDraw(DebugDrawInternal::GetLineCount(), DebugDrawInternal::GetLineVertexCount());
        DebugMetrics::RecordSolidDraw(DebugSolidDraw::GetTriangleCount(), DebugSolidDraw::GetVertexCount());
        DebugMetrics::RecordTextDraw(DebugTextDraw::GetEntryCount());
        DebugMetrics::RecordTimedEntry(DebugTimer::GetActiveCount());
    }

    void DebugDraw::Clear()
    {
        if (!s_Data)
            return;

        s_Data->LineVertices.clear();
        s_Data->LineVerticesNoDepth.clear();
        DebugSolidDraw::Clear();
        DebugTextDraw::Clear();
    }

    void DebugDraw::SetEnabled(bool enabled) { DebugSettings::Get().Enabled = enabled; }
    bool DebugDraw::IsEnabled() { return DebugSettings::Get().Enabled; }
    void DebugDraw::SetLineWidth(float width) { DebugSettings::Get().LineWidth = width; }
    float DebugDraw::GetLineWidth() { return DebugSettings::Get().LineWidth; }
    void DebugDraw::SetCameraPosition(const glm::vec3& cameraPosition)
    {
        if (s_Data)
            s_Data->CameraPosition = cameraPosition;
    }
    void DebugDraw::SetGridFadeRadius(float radius)
    {
        if (s_Data)
            s_Data->GridFadeRadius = radius;
    }
    void DebugDraw::SetGridStep(float step)
    {
        if (s_Data)
            s_Data->GridStep = step;
    }
    DebugSettings& DebugDraw::GetSettings() { return DebugSettings::Get(); }

    void DebugDraw::SolidCapsule(const glm::vec3& center, float radius, float height, const glm::vec4& color, float duration, bool depthTested)
    {
        if (duration > 0.0f)
        {
            DebugTimer::Submit(duration, depthTested, DebugCategory::Solid, [=]()
            {
                SolidCapsule(center, radius, height, color, 0.0f, depthTested);
            });
            return;
        }
        DebugSolidDraw::Capsule(center, radius, height, color, depthTested);
    }

    void DebugDraw::Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Circle(center, normal, radius, color, duration, depthTested);
    }

    void DebugDraw::Arc(const glm::vec3& center, const glm::vec3& normal, float radius, float startAngle, float endAngle, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Arc(center, normal, radius, startAngle, endAngle, color, duration, depthTested);
    }

    void DebugDraw::Cylinder(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Cylinder(start, end, radius, color, duration, depthTested);
    }

    void DebugDraw::Cone(const glm::vec3& tip, const glm::vec3& direction, float height, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Cone(tip, direction, height, radius, color, duration, depthTested);
    }

    void DebugDraw::Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Triangle(a, b, c, color, duration, depthTested);
    }

    void DebugDraw::Quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Quad(a, b, c, d, color, duration, depthTested);
    }

    void DebugDraw::Polygon(const std::vector<glm::vec3>& points, const glm::vec4& color, bool closed, float duration, bool depthTested)
    {
        DebugShapeDraw::Polygon(points, color, closed, duration, depthTested);
    }

    void DebugDraw::Path(const std::vector<glm::vec3>& points, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Path(points, color, duration, depthTested);
    }

    void DebugDraw::Bezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color, int segments, float duration, bool depthTested)
    {
        DebugShapeDraw::Bezier(p0, p1, p2, p3, color, segments, duration, depthTested);
    }

    void DebugDraw::Point(const glm::vec3& position, float size, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Point(position, size, color, duration, depthTested);
    }

    void DebugDraw::Torus(const glm::vec3& center, const glm::vec3& normal, float majorRadius, float minorRadius, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugShapeDraw::Torus(center, normal, majorRadius, minorRadius, color, duration, depthTested);
    }

    void DebugDraw::CoordinateAxes(const glm::vec3& origin, float length, float duration, bool depthTested)
    {
        DebugGizmoDraw::CoordinateAxes(origin, length, duration, depthTested);
    }

    void DebugDraw::CoordinateAxes(const glm::mat4& transform, float length, float duration, bool depthTested)
    {
        DebugGizmoDraw::CoordinateAxes(transform, length, duration, depthTested);
    }

    void DebugDraw::TransformGizmo(const glm::mat4& transform, float size, float duration, bool depthTested)
    {
        DebugGizmoDraw::TransformGizmo(transform, size, duration, depthTested);
    }

    void DebugDraw::RotationRing(const glm::vec3& center, const glm::vec3& axis, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugGizmoDraw::RotationRing(center, axis, radius, color, duration, depthTested);
    }

    void DebugDraw::Cross(const glm::vec3& center, float size, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugGizmoDraw::Cross(center, size, color, duration, depthTested);
    }

    void DebugDraw::Frustum(const glm::mat4& inverseViewProjection, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugCameraDraw::Frustum(inverseViewProjection, color, duration, depthTested);
    }

    void DebugDraw::FrustumFromMatrices(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugCameraDraw::FrustumFromMatrices(view, projection, color, duration, depthTested);
    }

    void DebugDraw::AABB(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugPhysicsDraw::AABB(min, max, color, duration, depthTested);
    }

    void DebugDraw::OBB(const glm::mat4& transform, const glm::vec3& halfExtents, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugPhysicsDraw::OBB(transform, halfExtents, color, duration, depthTested);
    }

    void DebugDraw::ContactPoint(const glm::vec3& point, const glm::vec3& normal, float size, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugPhysicsDraw::ContactPoint(point, normal, size, color, duration, depthTested);
    }

    void DebugDraw::Velocity(const glm::vec3& origin, const glm::vec3& velocity, float scale, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugPhysicsDraw::Velocity(origin, velocity, scale, color, duration, depthTested);
    }

    void DebugDraw::ClearTimed() { DebugTimer::Clear(); }
    void DebugDraw::ClearAll() { Clear(); ClearTimed(); }

    uint32_t DebugDraw::GetLineVertexCount() { return DebugDrawInternal::GetLineVertexCount(); }
    uint32_t DebugDraw::GetSolidTriangleCount() { return DebugSolidDraw::GetTriangleCount(); }
    uint32_t DebugDraw::GetTextEntryCount() { return DebugTextDraw::GetEntryCount(); }
    uint32_t DebugDraw::GetTimedEntryCount() { return DebugTimer::GetActiveCount(); }

    void DebugDraw::GridXZ(float size, float step, float duration, bool depthTested)
    {
        DebugGridDraw::XZ(size, step, DebugPalette::GridColor, DebugPalette::GridMajor, 5, duration, depthTested);
    }

    void DebugDraw::PolarGrid(const glm::vec3& center, float maxRadius, float radiusStep, float duration, bool depthTested)
    {
        DebugGridDraw::PolarXZ(center, maxRadius, radiusStep, static_cast<int>(DebugSettings::Get().CircleSegments), DebugPalette::GridColor, duration, depthTested);
    }
}
