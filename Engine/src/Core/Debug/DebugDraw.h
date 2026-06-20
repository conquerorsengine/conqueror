#pragma once

#include "Core/Base/Base.h"
#include "DebugSettings.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Conqueror
{
    class CQ_API DebugDraw
    {
    public:
        static void Init();
        static void Shutdown();

        // ── Settings ──
        static void SetEnabled(bool enabled);
        static bool IsEnabled();
        static void SetLineWidth(float width);
        static float GetLineWidth();
        static void SetCameraPosition(const glm::vec3& cameraPosition);
        static void SetGridFadeRadius(float radius);
        static void SetGridStep(float step);
        static DebugSettings& GetSettings();

        // ── Wireframe Shapes ──
        static void Line(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color = glm::vec4(1.0f), float duration = 0.0f, bool depthTested = true);
        
        static void Box(const glm::vec3& center, const glm::vec3& size, const glm::vec4& color = glm::vec4(1.0f), float duration = 0.0f, bool depthTested = true);
        static void Box(const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f), float duration = 0.0f, bool depthTested = true);
        
        static void Sphere(const glm::vec3& center, float radius, const glm::vec4& color = glm::vec4(1.0f), float duration = 0.0f, bool depthTested = true);
        
        static void Capsule(const glm::vec3& center, float radius, float height, const glm::vec4& color = glm::vec4(1.0f), float duration = 0.0f, bool depthTested = true);
        
        static void Ray(const glm::vec3& origin, const glm::vec3& direction, float length = 10.0f, const glm::vec4& color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), float duration = 0.0f, bool depthTested = true);
        
        static void Grid(float size = 10.0f, float step = 1.0f, const glm::vec4& color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), float duration = 0.0f, bool depthTested = true);
        static void GridXZ(float size = 10.0f, float step = 1.0f, float duration = 0.0f, bool depthTested = true);
        static void PolarGrid(const glm::vec3& center, float maxRadius = 10.0f, float radiusStep = 1.0f, float duration = 0.0f, bool depthTested = true);

        // ── Solid Shapes (Filled with blending) ──
        static void SolidBox(const glm::mat4& transform, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void SolidSphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void SolidCapsule(const glm::vec3& center, float radius, float height, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        // ── 3D Text ──
        static void Text3D(const glm::vec3& position, const std::string& text, const glm::vec4& color = glm::vec4(1.0f), float scale = 1.0f, float duration = 0.0f, bool depthTested = false);

        // ── Extended Wireframe Shapes ──
        static void Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Arc(const glm::vec3& center, const glm::vec3& normal, float radius, float startAngle, float endAngle, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Cylinder(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Cone(const glm::vec3& tip, const glm::vec3& direction, float height, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Polygon(const std::vector<glm::vec3>& points, const glm::vec4& color, bool closed = true, float duration = 0.0f, bool depthTested = true);
        static void Path(const std::vector<glm::vec3>& points, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Bezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color, int segments = 24, float duration = 0.0f, bool depthTested = true);
        static void Point(const glm::vec3& position, float size, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Torus(const glm::vec3& center, const glm::vec3& normal, float majorRadius, float minorRadius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        // ── Gizmo Helpers ──
        static void CoordinateAxes(const glm::vec3& origin, float length = 1.0f, float duration = 0.0f, bool depthTested = true);
        static void CoordinateAxes(const glm::mat4& transform, float length = 1.0f, float duration = 0.0f, bool depthTested = true);
        static void TransformGizmo(const glm::mat4& transform, float size = 1.0f, float duration = 0.0f, bool depthTested = true);
        static void RotationRing(const glm::vec3& center, const glm::vec3& axis, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Cross(const glm::vec3& center, float size, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        // ── Camera Helpers ──
        static void Frustum(const glm::mat4& inverseViewProjection, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void FrustumFromMatrices(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        // ── Physics Helpers ──
        static void AABB(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void OBB(const glm::mat4& transform, const glm::vec3& halfExtents, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void ContactPoint(const glm::vec3& point, const glm::vec3& normal, float size, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Velocity(const glm::vec3& origin, const glm::vec3& velocity, float scale = 1.0f, const glm::vec4& color = glm::vec4(0.2f, 0.8f, 1.0f, 0.9f), float duration = 0.0f, bool depthTested = true);

        // ── Lifecycle ──
        static void Update(float deltaTime); // Automatically removes expired debug items
        static void Render(const glm::mat4& viewProjection);
        static void Clear(); // Clears all current items instantly
        static void ClearTimed();
        static void ClearAll();

        // ── Stats ──
        static uint32_t GetLineVertexCount();
        static uint32_t GetSolidTriangleCount();
        static uint32_t GetTextEntryCount();
        static uint32_t GetTimedEntryCount();
    };
}
